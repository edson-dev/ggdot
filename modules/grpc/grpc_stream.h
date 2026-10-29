/**************************************************************************/
/*  grpc_stream.h                                                         */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#pragma once

#include "grpc_types.h"

#include "core/object/ref_counted.h"
#include "core/os/mutex.h"

class Thread;

// A bidirectional (or client-streaming) gRPC stream. Messages are exchanged as
// raw protobuf bytes; the pump runs on a worker thread.
class GrpcStream : public RefCounted {
	GDCLASS(GrpcStream, RefCounted);

	String host;
	int port = 443;
	bool use_tls = false;
	String server_name;
	String method;
	Vector<grpc::Header> metadata;
	int timeout_ms = 0;

	Thread *thread = nullptr;
	mutable Mutex mutex;
	Vector<Vector<uint8_t>> pending_out;
	Vector<Vector<uint8_t>> pending_messages;
	grpc::CallResult result;
	bool running = false;
	bool finished = false;
	bool close_send_requested = false;
	bool cancel_requested = false;
	bool headers_ready = false;
	bool closed_emitted = false;
	Dictionary pending_headers;

	static void _thread_callback(void *p_user) { static_cast<GrpcStream *>(p_user)->_thread_func(); }
	void _thread_func();
	void _emit_pending();

protected:
	static void _bind_methods();

public:
	GrpcStream() = default;
	~GrpcStream();

	void configure(const String &p_host, int p_port, bool p_use_tls, const String &p_server_name, const String &p_method,
			const Vector<grpc::Header> &p_metadata, int p_timeout_ms);
	Error start();

	Error send(const PackedByteArray &p_message);
	void close_send();
	void cancel();

	bool is_closed() const;
	Dictionary get_status() const;
	Dictionary get_headers() const;
	Dictionary get_trailers() const;
	Array get_messages() const;
};
