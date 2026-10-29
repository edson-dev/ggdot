/**************************************************************************/
/*  grpc_call.cpp                                                         */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#include "grpc_call.h"

#include "grpc_h2.h"
#include "grpc_util.h"

#include "core/object/class_db.h"
#include "core/os/os.h"
#include "core/os/thread.h"

void GrpcCall::_bind_methods() {
	ClassDB::bind_method(D_METHOD("start"), &GrpcCall::start);
	ClassDB::bind_method(D_METHOD("cancel"), &GrpcCall::cancel);
	ClassDB::bind_method(D_METHOD("is_running"), &GrpcCall::is_running);
	ClassDB::bind_method(D_METHOD("is_finished"), &GrpcCall::is_finished);
	ClassDB::bind_method(D_METHOD("get_status"), &GrpcCall::get_status);
	ClassDB::bind_method(D_METHOD("get_status_code"), &GrpcCall::get_status_code);
	ClassDB::bind_method(D_METHOD("get_status_message"), &GrpcCall::get_status_message);
	ClassDB::bind_method(D_METHOD("get_body"), &GrpcCall::get_body);
	ClassDB::bind_method(D_METHOD("get_messages"), &GrpcCall::get_messages);
	ClassDB::bind_method(D_METHOD("get_headers"), &GrpcCall::get_headers);
	ClassDB::bind_method(D_METHOD("get_trailers"), &GrpcCall::get_trailers);
	ClassDB::bind_method(D_METHOD("_emit_pending"), &GrpcCall::_emit_pending);

	ADD_SIGNAL(MethodInfo("completed", PropertyInfo(Variant::INT, "status_code")));
	ADD_SIGNAL(MethodInfo("message_received", PropertyInfo(Variant::PACKED_BYTE_ARRAY, "message")));
	ADD_SIGNAL(MethodInfo("headers_received", PropertyInfo(Variant::DICTIONARY, "headers")));
	ADD_SIGNAL(MethodInfo("trailers_received", PropertyInfo(Variant::DICTIONARY, "trailers")));
}

GrpcCall::~GrpcCall() {
	_stop_thread();
}

void GrpcCall::_stop_thread() {
	if (thread) {
		cancel_requested = true;
		thread->wait_to_finish();
		memdelete(thread);
		thread = nullptr;
	}
}

void GrpcCall::configure(const String &p_host, int p_port, bool p_use_tls, const String &p_server_name, const String &p_method,
		const Vector<uint8_t> &p_request, const Vector<grpc::Header> &p_metadata, int p_timeout_ms, bool p_server_streaming) {
	host = p_host;
	port = p_port;
	use_tls = p_use_tls;
	server_name = p_server_name;
	method = p_method;
	request_body = p_request;
	metadata = p_metadata;
	timeout_ms = p_timeout_ms > 0 ? p_timeout_ms : 8000;
	server_streaming = p_server_streaming;
}

Error GrpcCall::start() {
	if (thread) {
		return ERR_ALREADY_IN_USE;
	}
	{
		MutexLock lock(mutex);
		running = true;
		finished = false;
		completed_emitted = false;
		result = grpc::CallResult();
		pending_messages.clear();
	}
	thread = memnew(Thread);
	thread->start(&GrpcCall::_thread_callback, this);
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

void GrpcCall::cancel() {
	cancel_requested = true;
}

bool GrpcCall::is_running() const {
	MutexLock lock(mutex);
	return running;
}

bool GrpcCall::is_finished() const {
	MutexLock lock(mutex);
	return finished;
}

void GrpcCall::_thread_func() {
	grpc::H2Session session;
	Error err = session.start(host, port, use_tls, server_name, method, metadata, timeout_ms);
	Vector<Vector<uint8_t>> all_messages;

	if (err == OK) {
		session.send_message(request_body);
		session.close_send();

		while (!session.is_finished()) {
			if (cancel_requested) {
				session.cancel();
			}
			Error pump_err = session.pump(20);
			Vector<Vector<uint8_t>> messages = session.take_messages();
			if (!messages.is_empty()) {
				for (const Vector<uint8_t> &message : messages) {
					all_messages.push_back(message);
				}
				MutexLock lock(mutex);
				for (const Vector<uint8_t> &message : messages) {
					pending_messages.push_back(message);
				}
				call_deferred("_emit_pending");
			}
			if (pump_err != OK) {
				break;
			}
		}
	}

	grpc::CallResult call_result = session.get_result();
	call_result.messages = all_messages;
	if (cancel_requested) {
		call_result.status = grpc::Status(grpc::STATUS_CANCELLED, "call cancelled");
	}

	{
		MutexLock lock(mutex);
		result = call_result;
		running = false;
		finished = true;
	}

	call_deferred("_emit_pending");
}

void GrpcCall::_emit_pending() {
	Vector<Vector<uint8_t>> messages;
	Dictionary headers;
	Dictionary trailers;
	bool done = false;
	bool emit_completed = false;
	int code = grpc::STATUS_UNKNOWN;

	{
		MutexLock lock(mutex);
		messages = pending_messages;
		pending_messages.clear();
		done = finished;
		if (done && !completed_emitted) {
			completed_emitted = true;
			emit_completed = true;
		}
		code = result.status.code;
		headers = result.headers_to_dict();
		trailers = result.trailers_to_dict();
		emit_scheduled = false;
	}

	for (const Vector<uint8_t> &message : messages) {
		emit_signal("message_received", message);
	}

	if (emit_completed) {
		emit_signal("headers_received", headers);
		emit_signal("trailers_received", trailers);
		emit_signal("completed", code);
	}
}

Dictionary GrpcCall::get_status() const {
	MutexLock lock(mutex);
	return grpc::status_to_dict(result);
}

int GrpcCall::get_status_code() const {
	MutexLock lock(mutex);
	return result.status.code;
}

String GrpcCall::get_status_message() const {
	MutexLock lock(mutex);
	return result.status.message;
}

PackedByteArray GrpcCall::get_body() const {
	MutexLock lock(mutex);
	if (result.messages.is_empty()) {
		return PackedByteArray();
	}
	return result.messages[0];
}

Array GrpcCall::get_messages() const {
	MutexLock lock(mutex);
	Array out;
	for (const Vector<uint8_t> &message : result.messages) {
		out.push_back(message);
	}
	return out;
}

Dictionary GrpcCall::get_headers() const {
	MutexLock lock(mutex);
	return result.headers_to_dict();
}

Dictionary GrpcCall::get_trailers() const {
	MutexLock lock(mutex);
	return result.trailers_to_dict();
}
