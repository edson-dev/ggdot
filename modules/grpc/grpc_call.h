/**************************************************************************/
/*  grpc_call.h                                                           */
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

// A single asynchronous gRPC call. The network exchange runs on a worker
// thread; results are delivered on the main thread through signals.
class GrpcCall : public RefCounted {
	GDCLASS(GrpcCall, RefCounted);

public:
	enum Mode {
		MODE_UNARY,
		MODE_SERVER_STREAMING,
		MODE_CLIENT_STREAMING,
		MODE_BIDI,
	};

private:
	String host;
	int port = 443;
	bool use_tls = false;
	String server_name;
	String method;
	Vector<uint8_t> request_body;
	Vector<grpc::Header> metadata;
	int timeout_ms = 8000;
	bool server_streaming = false;

	Thread *thread = nullptr;
	mutable Mutex mutex;
	Vector<Vector<uint8_t>> pending_messages;
	grpc::CallResult result;
	bool running = false;
	bool finished = false;
	bool cancel_requested = false;
	bool emit_scheduled = false;
	bool completed_emitted = false;

	static void _thread_callback(void *p_user) { static_cast<GrpcCall *>(p_user)->_thread_func(); }
	void _thread_func();
	void _emit_pending();
	void _stop_thread();

protected:
	static void _bind_methods();

public:
	GrpcCall() = default;
	~GrpcCall();

	void configure(const String &p_host, int p_port, bool p_use_tls, const String &p_server_name, const String &p_method,
			const Vector<uint8_t> &p_request, const Vector<grpc::Header> &p_metadata, int p_timeout_ms, bool p_server_streaming);
	Error start();
	void cancel();

	bool is_running() const;
	bool is_finished() const;

	Dictionary get_status() const;
	int get_status_code() const;
	String get_status_message() const;
	PackedByteArray get_body() const;
	Array get_messages() const;
	Dictionary get_headers() const;
	Dictionary get_trailers() const;
};
