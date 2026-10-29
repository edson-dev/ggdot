/**************************************************************************/
/*  grpc_client.h                                                         */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#pragma once

#include "grpc_call.h"
#include "grpc_stream.h"
#include "grpc_types.h"
#include "proto/grpc_descriptor.h"

#include "core/object/ref_counted.h"

// Generic gRPC client: raw protobuf bytes plus reflection-driven JSON, matching
// the Go GDExtension API, with an asynchronous node/stream API on top.
class GrpcClient : public RefCounted {
	GDCLASS(GrpcClient, RefCounted);

	String host = "127.0.0.1";
	int port = 5051;
	bool use_tls = false;
	String server_name;
	bool configured = false;

	grpc::GrpcRegistry registry;
	grpc::CallResult last_result;
	Vector<Ref<GrpcCall>> active_calls;
	Vector<Ref<GrpcStream>> active_streams;

	bool _parse_address(const String &p_address);
	void _prune_active();
	grpc::CallResult _run(const String &p_method, const Vector<uint8_t> &p_request, const Vector<grpc::Header> &p_metadata,
			int p_timeout_ms);
	bool _ensure_method(const String &p_method, const grpc::GrpcMethod **r_method, const grpc::GrpcDescriptor **r_input,
			const grpc::GrpcDescriptor **r_output, String &r_error);
	bool _reflection_fetch(const String &p_service, String &r_error);
	String _error_json(const String &p_error) const;
	String _error_json(const grpc::CallResult &p_result) const;

protected:
	static void _bind_methods();

public:
	GrpcClient() = default;

	// Engine-style configuration.
	Error connect_to_host(const String &p_address, bool p_use_tls = false, const String &p_server_name = String());
	void close();
	Dictionary get_target() const;

	// Go GDExtension-compatible API.
	String connect_server(const String &p_address, bool p_use_tls, const String &p_server_name);
	void close_server();
	PackedByteArray invoke(const String &p_method, const PackedByteArray &p_request, const String &p_metadata_json, int p_timeout_ms);
	String invoke_json(const String &p_method, const String &p_request_json, const String &p_metadata_json, int p_timeout_ms);
	PackedByteArray invoke_server_stream(const String &p_method, const PackedByteArray &p_request, const String &p_metadata_json,
			int p_timeout_ms);
	String invoke_server_stream_json(const String &p_method, const String &p_request_json, const String &p_metadata_json,
			int p_timeout_ms);
	String last_status();

	// Async API.
	Ref<GrpcCall> call_async(const String &p_method, const PackedByteArray &p_request, const Dictionary &p_metadata = Dictionary(),
			int p_timeout_ms = 8000, bool p_server_streaming = false);
	Ref<GrpcStream> open_stream(const String &p_method, const PackedByteArray &p_request = PackedByteArray(),
			const Dictionary &p_metadata = Dictionary(), int p_timeout_ms = 0);
};
