/**************************************************************************/
/*  grpc_request.h                                                        */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#pragma once

#include "grpc_call.h"
#include "grpc_client.h"

#include "scene/main/node.h"

// Node-style gRPC request, the gRPC counterpart of HTTPRequest.
class GrpcRequest : public Node {
	GDCLASS(GrpcRequest, Node);

	Ref<GrpcClient> client;
	Ref<GrpcCall> call;
	bool requesting = false;

	void _on_completed(int p_status_code);
	void _on_message(const PackedByteArray &p_message);

protected:
	static void _bind_methods();

public:
	Error request(const String &p_address, const String &p_method, const PackedByteArray &p_request,
			const Dictionary &p_metadata = Dictionary(), int p_timeout_ms = 8000, bool p_use_tls = false,
			const String &p_server_name = String(), bool p_server_streaming = false);
	void cancel();
	bool is_requesting() const { return requesting; }

	Dictionary get_status() const;
	int get_status_code() const;
	String get_status_message() const;
	PackedByteArray get_body() const;
	Array get_messages() const;
	Dictionary get_headers() const;
	Dictionary get_trailers() const;
};
