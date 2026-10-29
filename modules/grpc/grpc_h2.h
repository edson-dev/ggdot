/**************************************************************************/
/*  grpc_h2.h                                                             */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#pragma once

#include "grpc_transport.h"
#include "grpc_types.h"

#include "core/os/mutex.h"

#include <nghttp2/nghttp2.h>

namespace grpc {

// One HTTP/2 connection carrying a single gRPC call. nghttp2 owns framing,
// HPACK and flow control; Transport owns TCP/TLS. The pump must be driven by
// a single thread; send_message() may be called from another thread.
class H2Session {
public:
	H2Session();
	~H2Session();

	// Submits the request. The first message must be queued with send_message()
	// before the first pump() call; unary calls must then call close_send().
	Error start(const String &p_host, int p_port, bool p_use_tls, const String &p_server_name,
			const String &p_method, const Vector<Header> &p_metadata, int p_timeout_ms);

	void send_message(const Vector<uint8_t> &p_message);
	void close_send();
	void cancel(uint32_t p_error_code = NGHTTP2_CANCEL);

	// Drives the connection for at most p_wait_ms milliseconds (or until the
	// call deadline). Returns ERR_TIMEOUT when the call deadline elapsed.
	Error pump(int p_wait_ms = 20);

	bool is_finished() const { return finished; }
	bool is_failed() const { return failed; }
	int deadline_remaining_ms() const;

	Vector<Vector<uint8_t>> take_messages();

	CallResult &get_result();
	// Marks the call as failed at the transport level.
	void set_transport_error(int p_code, const String &p_message);

	// Called from nghttp2 callbacks.
	int on_header(const nghttp2_frame *p_frame, const uint8_t *p_name, size_t p_namelen, const uint8_t *p_value, size_t p_valuelen);
	int on_data_chunk(const uint8_t *p_data, size_t p_length);
	int on_frame_recv(const nghttp2_frame *p_frame);
	int on_stream_close(int32_t p_stream_id, uint32_t p_error_code);
	nghttp2_ssize read_data(uint8_t *p_buffer, size_t p_length, uint32_t *r_data_flags);

private:
	Transport transport;
	nghttp2_session *session = nullptr;

	String host;
	int port = 443;
	bool use_tls = false;
	String server_name;
	String method;
	int timeout_ms = 8000;
	uint64_t start_ms = 0;
	uint64_t deadline_ms = 0;

	int32_t stream_id = -1;
	bool finished = false;
	bool failed = false;
	bool end_stream_seen = false;
	bool response_headers_seen = false;
	bool got_grpc_status = false;
	int grpc_status = -1;
	String grpc_message;

	CallResult result;
	Vector<uint8_t> rx_buffer;

	// Outbound message queue (client streaming).
	Mutex queue_mutex;
	Vector<Vector<uint8_t>> out_queue;
	Vector<uint8_t> current_frame;
	int current_offset = 0;
	bool local_closed = false;
	bool data_deferred = false;
	bool cancel_requested = false;

	int transport_error_code = STATUS_UNAVAILABLE;
	String transport_error_message;

	void _flush();
	void _parse_messages();
	void _resolve_status();
	void _cleanup();

	static int _on_header_cb(nghttp2_session *p_session, const nghttp2_frame *p_frame, const uint8_t *p_name, size_t p_namelen,
			const uint8_t *p_value, size_t p_valuelen, uint8_t p_flags, void *p_user_data);
	static int _on_data_chunk_cb(nghttp2_session *p_session, uint8_t p_flags, int32_t p_stream_id, const uint8_t *p_data,
			size_t p_length, void *p_user_data);
	static int _on_frame_recv_cb(nghttp2_session *p_session, const nghttp2_frame *p_frame, void *p_user_data);
	static int _on_stream_close_cb(nghttp2_session *p_session, int32_t p_stream_id, uint32_t p_error_code, void *p_user_data);
	static nghttp2_ssize _read_data_cb(nghttp2_session *p_session, int32_t p_stream_id, uint8_t *p_buffer, size_t p_length,
			uint32_t *r_data_flags, nghttp2_data_source *p_source, void *p_user_data);
};

} // namespace grpc
