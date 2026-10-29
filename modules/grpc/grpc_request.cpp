/**************************************************************************/
/*  grpc_request.cpp                                                      */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#include "grpc_request.h"

#include "core/object/class_db.h"
#include "core/object/callable_mp.h"

void GrpcRequest::_bind_methods() {
	ClassDB::bind_method(D_METHOD("request", "address", "method", "request", "metadata", "timeout_ms", "use_tls", "server_name",
								 "server_streaming"),
			&GrpcRequest::request, DEFVAL(Dictionary()), DEFVAL(8000), DEFVAL(false), DEFVAL(String()), DEFVAL(false));
	ClassDB::bind_method(D_METHOD("cancel"), &GrpcRequest::cancel);
	ClassDB::bind_method(D_METHOD("is_requesting"), &GrpcRequest::is_requesting);
	ClassDB::bind_method(D_METHOD("get_status"), &GrpcRequest::get_status);
	ClassDB::bind_method(D_METHOD("get_status_code"), &GrpcRequest::get_status_code);
	ClassDB::bind_method(D_METHOD("get_status_message"), &GrpcRequest::get_status_message);
	ClassDB::bind_method(D_METHOD("get_body"), &GrpcRequest::get_body);
	ClassDB::bind_method(D_METHOD("get_messages"), &GrpcRequest::get_messages);
	ClassDB::bind_method(D_METHOD("get_headers"), &GrpcRequest::get_headers);
	ClassDB::bind_method(D_METHOD("get_trailers"), &GrpcRequest::get_trailers);

	ADD_SIGNAL(MethodInfo("request_completed", PropertyInfo(Variant::INT, "status_code")));
	ADD_SIGNAL(MethodInfo("stream_message", PropertyInfo(Variant::PACKED_BYTE_ARRAY, "message")));
}

Error GrpcRequest::request(const String &p_address, const String &p_method, const PackedByteArray &p_request,
		const Dictionary &p_metadata, int p_timeout_ms, bool p_use_tls, const String &p_server_name, bool p_server_streaming) {
	if (requesting) {
		return ERR_BUSY;
	}
	client.instantiate();
	Error err = client->connect_to_host(p_address, p_use_tls, p_server_name);
	if (err != OK) {
		return err;
	}
	call = client->call_async(p_method, p_request, p_metadata, p_timeout_ms, p_server_streaming);
	if (call.is_null()) {
		return ERR_CANT_CREATE;
	}
	call->connect("completed", callable_mp(this, &GrpcRequest::_on_completed));
	call->connect("message_received", callable_mp(this, &GrpcRequest::_on_message));
	requesting = true;
	return OK;
}

void GrpcRequest::cancel() {
	if (call.is_valid()) {
		call->cancel();
	}
}

void GrpcRequest::_on_completed(int p_status_code) {
	requesting = false;
	emit_signal("request_completed", p_status_code);
}

void GrpcRequest::_on_message(const PackedByteArray &p_message) {
	emit_signal("stream_message", p_message);
}

Dictionary GrpcRequest::get_status() const {
	return call.is_valid() ? call->get_status() : Dictionary();
}

int GrpcRequest::get_status_code() const {
	return call.is_valid() ? call->get_status_code() : grpc::STATUS_UNAVAILABLE;
}

String GrpcRequest::get_status_message() const {
	return call.is_valid() ? call->get_status_message() : String();
}

PackedByteArray GrpcRequest::get_body() const {
	return call.is_valid() ? call->get_body() : PackedByteArray();
}

Array GrpcRequest::get_messages() const {
	return call.is_valid() ? call->get_messages() : Array();
}

Dictionary GrpcRequest::get_headers() const {
	return call.is_valid() ? call->get_headers() : Dictionary();
}

Dictionary GrpcRequest::get_trailers() const {
	return call.is_valid() ? call->get_trailers() : Dictionary();
}
