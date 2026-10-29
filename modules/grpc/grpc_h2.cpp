/**************************************************************************/
/*  grpc_h2.cpp                                                           */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#include "grpc_h2.h"

#include "core/os/os.h"
#include "core/version.h"

#include <cstring>

namespace grpc {

static uint64_t now_ms() {
	return OS::get_singleton()->get_ticks_msec();
}

static void append_u32_be(Vector<uint8_t> &r_buffer, uint32_t p_value) {
	r_buffer.push_back((p_value >> 24) & 0xff);
	r_buffer.push_back((p_value >> 16) & 0xff);
	r_buffer.push_back((p_value >> 8) & 0xff);
	r_buffer.push_back(p_value & 0xff);
}

H2Session::H2Session() = default;

H2Session::~H2Session() {
	_cleanup();
}

void H2Session::_cleanup() {
	if (session) {
		nghttp2_session_del(session);
		session = nullptr;
	}
	transport.close();
}

Error H2Session::start(const String &p_host, int p_port, bool p_use_tls, const String &p_server_name,
		const String &p_method, const Vector<Header> &p_metadata, int p_timeout_ms) {
	host = p_host;
	port = p_port;
	use_tls = p_use_tls;
	server_name = p_server_name.is_empty() ? p_host : p_server_name;
	method = p_method;
	timeout_ms = p_timeout_ms > 0 ? p_timeout_ms : 0;

	start_ms = now_ms();
	deadline_ms = timeout_ms > 0 ? start_ms + (uint64_t)timeout_ms : UINT64_MAX;

	Error err = transport.connect_to_host(host, port, use_tls, server_name, deadline_ms);
	if (err != OK) {
		transport_error_code = (err == ERR_TIMEOUT) ? STATUS_DEADLINE_EXCEEDED : STATUS_UNAVAILABLE;
		transport_error_message = err == ERR_TIMEOUT ? "connection timed out" : "could not connect to " + host + ":" + itos(port);
		failed = true;
		return err;
	}

	nghttp2_session_callbacks *callbacks = nullptr;
	nghttp2_session_callbacks_new(&callbacks);
	nghttp2_session_callbacks_set_on_header_callback(callbacks, _on_header_cb);
	nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks, _on_data_chunk_cb);
	nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks, _on_frame_recv_cb);
	nghttp2_session_callbacks_set_on_stream_close_callback(callbacks, _on_stream_close_cb);
	int rv = nghttp2_session_client_new(&session, callbacks, this);
	nghttp2_session_callbacks_del(callbacks);
	if (rv != 0) {
		_cleanup();
		transport_error_message = "could not create the HTTP/2 session";
		failed = true;
		return ERR_CANT_CREATE;
	}

	nghttp2_settings_entry settings[] = {
		{ NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE, 4 * 1024 * 1024 },
	};
	nghttp2_submit_settings(session, NGHTTP2_FLAG_NONE, settings, 1);

	Vector<Header> headers;
	headers.push_back(Header(":method", "POST"));
	headers.push_back(Header(":scheme", use_tls ? "https" : "http"));
	headers.push_back(Header(":path", method));
	headers.push_back(Header(":authority", use_tls && port == 443 ? server_name : host + ":" + itos(port)));
	headers.push_back(Header("content-type", "application/grpc"));
	headers.push_back(Header("te", "trailers"));
	headers.push_back(Header("user-agent", "godot-grpc/" + String(GODOT_VERSION_FULL_CONFIG)));
	String timeout = timeout_header(timeout_ms);
	if (!timeout.is_empty()) {
		headers.push_back(Header("grpc-timeout", timeout));
	}
	for (const Header &h : p_metadata) {
		if (h.name.is_empty() || h.name.begins_with(":")) {
			continue;
		}
		headers.push_back(h);
	}

	Vector<Vector<uint8_t>> storage;
	storage.resize(headers.size() * 2);
	Vector<nghttp2_nv> nva;
	nva.resize(headers.size());
	for (int i = 0; i < headers.size(); i++) {
		storage.ptrw()[i * 2] = headers[i].name.to_lower().to_utf8_buffer();
		storage.ptrw()[i * 2 + 1] = headers[i].value.to_utf8_buffer();
		nghttp2_nv &nv = nva.ptrw()[i];
		nv.name = storage.ptrw()[i * 2].ptrw();
		nv.namelen = storage[i * 2].size();
		nv.value = storage.ptrw()[i * 2 + 1].ptrw();
		nv.valuelen = storage[i * 2 + 1].size();
		nv.flags = NGHTTP2_NV_FLAG_NONE;
	}

	nghttp2_data_provider2 provider;
	provider.source.ptr = this;
	provider.read_callback = _read_data_cb;

	stream_id = nghttp2_submit_request2(session, nullptr, nva.ptr(), nva.size(), &provider, this);
	if (stream_id < 0) {
		_cleanup();
		transport_error_message = "could not submit the gRPC request";
		failed = true;
		return ERR_CANT_CREATE;
	}

	return OK;
}

void H2Session::send_message(const Vector<uint8_t> &p_message) {
	MutexLock lock(queue_mutex);
	Vector<uint8_t> framed;
	framed.resize(p_message.size() + 5);
	framed.write[0] = 0;
	uint32_t size = p_message.size();
	framed.write[1] = (size >> 24) & 0xff;
	framed.write[2] = (size >> 16) & 0xff;
	framed.write[3] = (size >> 8) & 0xff;
	framed.write[4] = size & 0xff;
	if (size > 0) {
		memcpy(framed.ptrw() + 5, p_message.ptr(), size);
	}
	out_queue.push_back(framed);
}

void H2Session::close_send() {
	MutexLock lock(queue_mutex);
	local_closed = true;
}

void H2Session::cancel(uint32_t p_error_code) {
	// Only a flag is touched here: nghttp2 must be driven by the pump thread.
	cancel_requested = true;
	(void)p_error_code;
}

int H2Session::deadline_remaining_ms() const {
	if (deadline_ms == UINT64_MAX) {
		return 0;
	}
	uint64_t now = now_ms();
	if (now >= deadline_ms) {
		return 0;
	}
	return (int)(deadline_ms - now);
}

Error H2Session::pump(int p_wait_ms) {
	if (!session) {
		return ERR_UNCONFIGURED;
	}
	if (finished) {
		return OK;
	}
	if (now_ms() >= deadline_ms && deadline_ms != UINT64_MAX) {
		set_transport_error(STATUS_DEADLINE_EXCEEDED, "deadline exceeded");
		return ERR_TIMEOUT;
	}

	if (cancel_requested) {
		cancel_requested = false;
		if (stream_id > 0) {
			nghttp2_submit_rst_stream(session, NGHTTP2_FLAG_NONE, stream_id, NGHTTP2_CANCEL);
		}
		set_transport_error(STATUS_CANCELLED, "call cancelled");
		_flush();
		return ERR_SKIP;
	}

	{
		MutexLock lock(queue_mutex);
		bool has_data = !out_queue.is_empty() || !current_frame.is_empty();
		if (data_deferred && (has_data || local_closed) && stream_id > 0) {
			nghttp2_session_resume_data(session, stream_id);
			data_deferred = false;
		}
	}

	_flush();
	if (failed) {
		return ERR_CONNECTION_ERROR;
	}

	if (finished) {
		return OK;
	}

	uint64_t wait_deadline = MIN(now_ms() + (uint64_t)MAX(1, p_wait_ms), deadline_ms);
	if (wait_deadline == UINT64_MAX) {
		wait_deadline = now_ms() + (uint64_t)MAX(1, p_wait_ms);
	}
	uint8_t buffer[32768];
	int received = transport.read_some(buffer, sizeof(buffer), wait_deadline);
	if (received < 0) {
		set_transport_error(STATUS_UNAVAILABLE, "connection closed by the peer");
		return ERR_CONNECTION_ERROR;
	}
	if (received > 0) {
		ssize_t total = 0;
		while (total < received) {
			nghttp2_ssize consumed = nghttp2_session_mem_recv2(session, buffer + total, (size_t)(received - total));
			if (consumed < 0) {
				set_transport_error(STATUS_INTERNAL, "HTTP/2 protocol error");
				return ERR_BUG;
			}
			if (consumed == 0) {
				break;
			}
			total += consumed;
		}
	}

	if (nghttp2_session_want_read(session) == 0 && nghttp2_session_want_write(session) == 0) {
		finished = true;
	}

	return OK;
}

void H2Session::_flush() {
	while (session) {
		const uint8_t *data = nullptr;
		nghttp2_ssize length = nghttp2_session_mem_send2(session, &data);
		if (length < 0) {
			set_transport_error(STATUS_INTERNAL, "HTTP/2 send error");
			return;
		}
		if (length == 0) {
			break;
		}
		Error err = transport.write_all(data, (int)length, deadline_ms);
		if (err != OK) {
			set_transport_error(err == ERR_TIMEOUT ? STATUS_DEADLINE_EXCEEDED : STATUS_UNAVAILABLE,
					err == ERR_TIMEOUT ? "deadline exceeded" : "could not write to the connection");
			return;
		}
	}
}

void H2Session::_parse_messages() {
	int offset = 0;
	const int size = rx_buffer.size();
	while (size - offset >= 5) {
		int compressed = rx_buffer[offset];
		uint32_t length = ((uint32_t)rx_buffer[offset + 1] << 24) | ((uint32_t)rx_buffer[offset + 2] << 16) |
				((uint32_t)rx_buffer[offset + 3] << 8) | (uint32_t)rx_buffer[offset + 4];
		if (length > 256 * 1024 * 1024) {
			set_transport_error(STATUS_RESOURCE_EXHAUSTED, "gRPC message is too large");
			return;
		}
		if ((uint32_t)(size - offset - 5) < length) {
			break;
		}
		if (compressed != 0) {
			set_transport_error(STATUS_UNIMPLEMENTED, "compressed gRPC messages are not supported");
			return;
		}
		Vector<uint8_t> message;
		message.resize(length);
		if (length > 0) {
			memcpy(message.ptrw(), rx_buffer.ptr() + offset + 5, length);
		}
		result.messages.push_back(message);
		offset += 5 + length;
	}
	if (offset > 0) {
		int remaining = size - offset;
		if (remaining <= 0) {
			rx_buffer.clear();
		} else {
			Vector<uint8_t> rest;
			rest.resize(remaining);
			memcpy(rest.ptrw(), rx_buffer.ptr() + offset, remaining);
			rx_buffer = rest;
		}
	}
}

Vector<Vector<uint8_t>> H2Session::take_messages() {
	Vector<Vector<uint8_t>> out = result.messages;
	result.messages.clear();
	return out;
}

void H2Session::set_transport_error(int p_code, const String &p_message) {
	if (failed) {
		return;
	}
	failed = true;
	transport_error_code = p_code;
	transport_error_message = p_message;
	if (timeout_ms > 0 && now_ms() >= deadline_ms && p_code != STATUS_DEADLINE_EXCEEDED) {
		transport_error_code = STATUS_DEADLINE_EXCEEDED;
		transport_error_message = "deadline exceeded";
	}
}

CallResult &H2Session::get_result() {
	_resolve_status();
	return result;
}

void H2Session::_resolve_status() {
	if (got_grpc_status) {
		result.status = Status(grpc_status, grpc_message);
		return;
	}
	if (failed) {
		result.status = Status(transport_error_code, transport_error_message);
		return;
	}
	if (result.http_status != 0 && result.http_status != 200) {
		result.status = Status(http_status_to_grpc(result.http_status), "HTTP status " + itos(result.http_status));
		return;
	}
	if (finished) {
		result.status = Status(STATUS_UNKNOWN, "server closed the stream without a gRPC status");
		return;
	}
	result.status = Status(STATUS_INTERNAL, "call did not complete");
}

int H2Session::on_header(const nghttp2_frame *p_frame, const uint8_t *p_name, size_t p_namelen, const uint8_t *p_value, size_t p_valuelen) {
	String name = String::utf8((const char *)p_name, (int)p_namelen);
	String value = String::utf8((const char *)p_value, (int)p_valuelen);

	if (name == ":status") {
		result.http_status = value.to_int();
		response_headers_seen = true;
		return 0;
	}
	if (name == "grpc-status") {
		got_grpc_status = true;
		grpc_status = value.to_int();
		return 0;
	}
	if (name == "grpc-message") {
		grpc_message = decode_grpc_message(value);
		return 0;
	}
	if (name.begins_with(":")) {
		return 0;
	}

	bool trailer = p_frame->headers.cat != NGHTTP2_HCAT_RESPONSE;
	result.add_header(name, value, trailer);
	return 0;
}

int H2Session::on_data_chunk(const uint8_t *p_data, size_t p_length) {
	int previous = rx_buffer.size();
	rx_buffer.resize(previous + (int)p_length);
	memcpy(rx_buffer.ptrw() + previous, p_data, p_length);
	_parse_messages();
	return 0;
}

int H2Session::on_frame_recv(const nghttp2_frame *p_frame) {
	if (p_frame->hd.type == NGHTTP2_GOAWAY) {
		set_transport_error(STATUS_UNAVAILABLE, "server sent GOAWAY");
	}
	if (p_frame->hd.type == NGHTTP2_HEADERS || p_frame->hd.type == NGHTTP2_DATA) {
		if (p_frame->hd.flags & NGHTTP2_FLAG_END_STREAM) {
			end_stream_seen = true;
		}
	}
	return 0;
}

int H2Session::on_stream_close(int32_t p_stream_id, uint32_t p_error_code) {
	if (p_stream_id != stream_id) {
		return 0;
	}
	finished = true;
	if (p_error_code != NGHTTP2_NO_ERROR && !got_grpc_status && !failed) {
		int code = STATUS_INTERNAL;
		if (p_error_code == NGHTTP2_CANCEL) {
			code = STATUS_CANCELLED;
		} else if (p_error_code == NGHTTP2_REFUSED_STREAM) {
			code = STATUS_UNAVAILABLE;
		}
		set_transport_error(code, "stream closed with HTTP/2 error " + itos((int)p_error_code));
	}
	return 0;
}

nghttp2_ssize H2Session::read_data(uint8_t *p_buffer, size_t p_length, uint32_t *r_data_flags) {
	MutexLock lock(queue_mutex);

	while (current_frame.is_empty() || current_offset >= current_frame.size()) {
		current_frame.clear();
		current_offset = 0;
		if (!out_queue.is_empty()) {
			current_frame = out_queue[0];
			out_queue.remove_at(0);
			continue;
		}
		break;
	}

	if (current_frame.is_empty()) {
		if (local_closed) {
			*r_data_flags |= NGHTTP2_DATA_FLAG_EOF;
			return 0;
		}
		data_deferred = true;
		return NGHTTP2_ERR_DEFERRED;
	}

	int available = current_frame.size() - current_offset;
	int to_copy = MIN((int)p_length, available);
	memcpy(p_buffer, current_frame.ptr() + current_offset, to_copy);
	current_offset += to_copy;

	if (current_offset >= current_frame.size()) {
		current_frame.clear();
		current_offset = 0;
		if (out_queue.is_empty() && local_closed) {
			*r_data_flags |= NGHTTP2_DATA_FLAG_EOF;
		}
	}

	return to_copy;
}

int H2Session::_on_header_cb(nghttp2_session *p_session, const nghttp2_frame *p_frame, const uint8_t *p_name, size_t p_namelen,
		const uint8_t *p_value, size_t p_valuelen, uint8_t p_flags, void *p_user_data) {
	return static_cast<H2Session *>(p_user_data)->on_header(p_frame, p_name, p_namelen, p_value, p_valuelen);
}

int H2Session::_on_data_chunk_cb(nghttp2_session *p_session, uint8_t p_flags, int32_t p_stream_id, const uint8_t *p_data,
		size_t p_length, void *p_user_data) {
	return static_cast<H2Session *>(p_user_data)->on_data_chunk(p_data, p_length);
}

int H2Session::_on_frame_recv_cb(nghttp2_session *p_session, const nghttp2_frame *p_frame, void *p_user_data) {
	return static_cast<H2Session *>(p_user_data)->on_frame_recv(p_frame);
}

int H2Session::_on_stream_close_cb(nghttp2_session *p_session, int32_t p_stream_id, uint32_t p_error_code, void *p_user_data) {
	return static_cast<H2Session *>(p_user_data)->on_stream_close(p_stream_id, p_error_code);
}

nghttp2_ssize H2Session::_read_data_cb(nghttp2_session *p_session, int32_t p_stream_id, uint8_t *p_buffer, size_t p_length,
		uint32_t *r_data_flags, nghttp2_data_source *p_source, void *p_user_data) {
	return static_cast<H2Session *>(p_user_data)->read_data(p_buffer, p_length, r_data_flags);
}

} // namespace grpc
