/**************************************************************************/
/*  grpc_stream.cpp                                                       */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#include "grpc_stream.h"

#include "grpc_h2.h"
#include "grpc_util.h"

#include "core/object/class_db.h"
#include "core/os/thread.h"

void GrpcStream::_bind_methods() {
	ClassDB::bind_method(D_METHOD("send", "message"), &GrpcStream::send);
	ClassDB::bind_method(D_METHOD("close_send"), &GrpcStream::close_send);
	ClassDB::bind_method(D_METHOD("cancel"), &GrpcStream::cancel);
	ClassDB::bind_method(D_METHOD("is_closed"), &GrpcStream::is_closed);
	ClassDB::bind_method(D_METHOD("get_status"), &GrpcStream::get_status);
	ClassDB::bind_method(D_METHOD("get_headers"), &GrpcStream::get_headers);
	ClassDB::bind_method(D_METHOD("get_trailers"), &GrpcStream::get_trailers);
	ClassDB::bind_method(D_METHOD("get_messages"), &GrpcStream::get_messages);
	ClassDB::bind_method(D_METHOD("_emit_pending"), &GrpcStream::_emit_pending);

	ADD_SIGNAL(MethodInfo("message", PropertyInfo(Variant::PACKED_BYTE_ARRAY, "message")));
	ADD_SIGNAL(MethodInfo("headers", PropertyInfo(Variant::DICTIONARY, "headers")));
	ADD_SIGNAL(MethodInfo("closed", PropertyInfo(Variant::DICTIONARY, "status")));
}

GrpcStream::~GrpcStream() {
	if (thread) {
		cancel_requested = true;
		thread->wait_to_finish();
		memdelete(thread);
		thread = nullptr;
	}
}

void GrpcStream::configure(const String &p_host, int p_port, bool p_use_tls, const String &p_server_name, const String &p_method,
		const Vector<grpc::Header> &p_metadata, int p_timeout_ms) {
	host = p_host;
	port = p_port;
	use_tls = p_use_tls;
	server_name = p_server_name;
	method = p_method;
	metadata = p_metadata;
	timeout_ms = p_timeout_ms > 0 ? p_timeout_ms : 0;
}

Error GrpcStream::start() {
	if (thread) {
		return ERR_ALREADY_IN_USE;
	}
	{
		MutexLock lock(mutex);
		running = true;
		finished = false;
		closed_emitted = false;
	}
	thread = memnew(Thread);
	thread->start(&GrpcStream::_thread_callback, this);
	if (!thread->is_started()) {
		memdelete(thread);
		thread = nullptr;
		MutexLock lock(mutex);
		running = false;
		finished = true;
		return ERR_CANT_CREATE;
	}
	return OK;
}

Error GrpcStream::send(const PackedByteArray &p_message) {
	MutexLock lock(mutex);
	if (finished || close_send_requested) {
		return ERR_UNCONFIGURED;
	}
	pending_out.push_back(p_message);
	return OK;
}

void GrpcStream::close_send() {
	MutexLock lock(mutex);
	close_send_requested = true;
}

void GrpcStream::cancel() {
	cancel_requested = true;
}

bool GrpcStream::is_closed() const {
	MutexLock lock(mutex);
	return finished;
}

Dictionary GrpcStream::get_status() const {
	MutexLock lock(mutex);
	return grpc::status_to_dict(result);
}

Dictionary GrpcStream::get_headers() const {
	MutexLock lock(mutex);
	return result.headers_to_dict();
}

Dictionary GrpcStream::get_trailers() const {
	MutexLock lock(mutex);
	return result.trailers_to_dict();
}

Array GrpcStream::get_messages() const {
	MutexLock lock(mutex);
	Array out;
	for (const Vector<uint8_t> &message : result.messages) {
		out.push_back(message);
	}
	return out;
}

void GrpcStream::_thread_func() {
	grpc::H2Session session;
	Error err = session.start(host, port, use_tls, server_name, method, metadata, timeout_ms);
	Vector<Vector<uint8_t>> all_messages;
	bool headers_sent = false;

	while (err == OK && !session.is_finished()) {
		Vector<Vector<uint8_t>> outgoing;
		bool close_after = false;
		{
			MutexLock lock(mutex);
			outgoing = pending_out;
			pending_out.clear();
			close_after = close_send_requested;
		}
		for (const Vector<uint8_t> &message : outgoing) {
			session.send_message(message);
		}
		if (close_after) {
			session.close_send();
		}
		if (cancel_requested) {
			session.cancel();
		}

		Error pump_err = session.pump(10);
		Vector<Vector<uint8_t>> messages = session.take_messages();
		Dictionary headers;
		bool new_headers = false;

		if (!messages.is_empty() || (!headers_sent && session.get_result().http_status != 0)) {
			MutexLock lock(mutex);
			for (const Vector<uint8_t> &message : messages) {
				all_messages.push_back(message);
				pending_messages.push_back(message);
			}
			if (!headers_sent && session.get_result().http_status != 0) {
				pending_headers = session.get_result().headers_to_dict();
				headers_ready = true;
				new_headers = true;
				headers_sent = true;
			}
		}

		if (new_headers || !messages.is_empty()) {
			call_deferred("_emit_pending");
		}

		if (pump_err != OK) {
			break;
		}
	}

	grpc::CallResult call_result = session.get_result();
	call_result.messages = all_messages;
	if (cancel_requested) {
		call_result.status = grpc::Status(grpc::STATUS_CANCELLED, "stream cancelled");
	}

	{
		MutexLock lock(mutex);
		result = call_result;
		running = false;
		finished = true;
	}

	call_deferred("_emit_pending");
}

void GrpcStream::_emit_pending() {
	Vector<Vector<uint8_t>> messages;
	bool done = false;
	bool emit_closed = false;
	bool had_headers = false;
	Dictionary headers;
	Dictionary status;

	{
		MutexLock lock(mutex);
		messages = pending_messages;
		pending_messages.clear();
		had_headers = headers_ready;
		headers = pending_headers;
		headers_ready = false;
		pending_headers = Dictionary();
		done = finished;
		if (done && !closed_emitted) {
			closed_emitted = true;
			emit_closed = true;
			status = grpc::status_to_dict(result);
		}
	}

	if (had_headers) {
		emit_signal("headers", headers);
	}

	for (const Vector<uint8_t> &message : messages) {
		emit_signal("message", message);
	}

	if (emit_closed) {
		emit_signal("closed", status);
	}
}
