/**************************************************************************/
/*  grpc_client.cpp                                                       */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#include "grpc_client.h"

#include "grpc_h2.h"
#include "grpc_util.h"
#include "proto/grpc_binary.h"
#include "proto/grpc_json.h"

#include "core/object/class_db.h"
#include "core/io/json.h"

#include <cstring>

static String normalize_method(const String &p_method) {
	String method = p_method.strip_edges();
	if (method.is_empty()) {
		return String();
	}
	if (!method.begins_with("/")) {
		method = "/" + method;
	}
	return method;
}

static bool split_method(const String &p_method, String &r_service, String &r_name) {
	String method = normalize_method(p_method);
	int slash = method.rfind("/");
	if (slash <= 0 || slash + 1 >= method.length()) {
		return false;
	}
	r_service = method.substr(1, slash - 1);
	r_name = method.substr(slash + 1);
	return !r_service.is_empty() && !r_name.is_empty();
}

void GrpcClient::_bind_methods() {
	for (int i = grpc::STATUS_OK; i <= grpc::STATUS_UNAUTHENTICATED; i++) {
		ClassDB::bind_integer_constant("GrpcClient", "Status", grpc::status_name(i), i);
	}

	ClassDB::bind_method(D_METHOD("connect_to_host", "address", "use_tls", "server_name"), &GrpcClient::connect_to_host,
			DEFVAL(false), DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("close"), &GrpcClient::close);
	ClassDB::bind_method(D_METHOD("get_target"), &GrpcClient::get_target);

	ClassDB::bind_method(D_METHOD("connect_server", "address", "use_tls", "server_name"), &GrpcClient::connect_server);
	ClassDB::bind_method(D_METHOD("close_server"), &GrpcClient::close_server);
	ClassDB::bind_method(D_METHOD("invoke", "method", "request", "metadata_json", "timeout_ms"), &GrpcClient::invoke);
	ClassDB::bind_method(D_METHOD("invoke_json", "method", "request_json", "metadata_json", "timeout_ms"), &GrpcClient::invoke_json);
	ClassDB::bind_method(D_METHOD("invoke_server_stream", "method", "request", "metadata_json", "timeout_ms"),
			&GrpcClient::invoke_server_stream);
	ClassDB::bind_method(D_METHOD("invoke_server_stream_json", "method", "request_json", "metadata_json", "timeout_ms"),
			&GrpcClient::invoke_server_stream_json);
	ClassDB::bind_method(D_METHOD("last_status"), &GrpcClient::last_status);

	ClassDB::bind_method(D_METHOD("call_async", "method", "request", "metadata", "timeout_ms", "server_streaming"), &GrpcClient::call_async,
			DEFVAL(Dictionary()), DEFVAL(8000), DEFVAL(false));
	ClassDB::bind_method(D_METHOD("open_stream", "method", "request", "metadata", "timeout_ms"), &GrpcClient::open_stream,
			DEFVAL(PackedByteArray()), DEFVAL(Dictionary()), DEFVAL(0));
}

bool GrpcClient::_parse_address(const String &p_address) {
	String address = p_address.strip_edges();
	if (address.is_empty()) {
		return false;
	}
	int colon = address.rfind(":");
	bool has_port = colon > 0 && colon + 1 < address.length();
	if (address.begins_with("[") && address.ends_with("]")) {
		has_port = false;
	}
	if (address.begins_with("[") && colon > 0 && address.find("]") < colon) {
		has_port = true;
	}
	if (has_port) {
		String port_text = address.substr(colon + 1);
		if (!port_text.is_valid_int()) {
			return false;
		}
		host = address.substr(0, colon);
		port = port_text.to_int();
	} else {
		host = address;
		port = use_tls ? 443 : 80;
	}
	if (host.begins_with("[") && host.ends_with("]")) {
		host = host.substr(1, host.length() - 2);
	}
	if (host.is_empty() || port <= 0 || port > 65535) {
		return false;
	}
	if (server_name.is_empty()) {
		server_name = host;
	}
	return true;
}

Error GrpcClient::connect_to_host(const String &p_address, bool p_use_tls, const String &p_server_name) {
	use_tls = p_use_tls;
	server_name = p_server_name;
	if (!_parse_address(p_address)) {
		configured = false;
		return ERR_INVALID_PARAMETER;
	}
	configured = true;
	registry.clear();
	last_result = grpc::CallResult();
	return OK;
}

void GrpcClient::close() {
	active_calls.clear();
	active_streams.clear();
	registry.clear();
	configured = false;
}

Dictionary GrpcClient::get_target() const {
	Dictionary out;
	out["host"] = host;
	out["port"] = port;
	out["use_tls"] = use_tls;
	out["server_name"] = server_name;
	return out;
}

String GrpcClient::connect_server(const String &p_address, bool p_use_tls, const String &p_server_name) {
	Error err = connect_to_host(p_address, p_use_tls, p_server_name);
	if (err != OK) {
		return _error_json("invalid gRPC address: " + p_address);
	}
	return "{\"ok\":true}";
}

void GrpcClient::close_server() {
	close();
}

String GrpcClient::last_status() {
	return grpc::status_to_json(last_result);
}

String GrpcClient::_error_json(const String &p_error) const {
	Dictionary out;
	out["ok"] = false;
	out["error"] = p_error;
	return JSON::stringify(out);
}

String GrpcClient::_error_json(const grpc::CallResult &p_result) const {
	Dictionary out;
	out["ok"] = false;
	out["error"] = p_result.status.message.is_empty() ? grpc::status_name(p_result.status.code) : p_result.status.message;
	out["code"] = p_result.status.code;
	out["status"] = grpc::status_name(p_result.status.code);
	return JSON::stringify(out);
}

grpc::CallResult GrpcClient::_run(const String &p_method, const Vector<uint8_t> &p_request, const Vector<grpc::Header> &p_metadata,
		int p_timeout_ms) {
	grpc::CallResult result;
	if (!configured) {
		result.status = grpc::Status(grpc::STATUS_UNAVAILABLE, "gRPC client is not connected");
		return result;
	}

	grpc::H2Session session;
	Error err = session.start(host, port, use_tls, server_name, normalize_method(p_method), p_metadata, p_timeout_ms);
	if (err != OK) {
		result = session.get_result();
		last_result = result;
		return result;
	}

	session.send_message(p_request);
	session.close_send();

	while (!session.is_finished()) {
		Error pump_err = session.pump(50);
		if (pump_err != OK) {
			break;
		}
	}

	Vector<Vector<uint8_t>> messages = session.take_messages();
	result = session.get_result();
	result.messages = messages;
	last_result = result;
	return result;
}

PackedByteArray GrpcClient::invoke(const String &p_method, const PackedByteArray &p_request, const String &p_metadata_json,
		int p_timeout_ms) {
	String metadata_error;
	Vector<grpc::Header> metadata = grpc::metadata_from_json(p_metadata_json, metadata_error);
	if (!metadata_error.is_empty()) {
		grpc::CallResult error_result;
		error_result.status = grpc::Status(grpc::STATUS_INVALID_ARGUMENT, metadata_error);
		last_result = error_result;
		return PackedByteArray();
	}
	grpc::CallResult result = _run(p_method, p_request, metadata, p_timeout_ms);
	if (!result.status.ok() || result.messages.is_empty()) {
		return PackedByteArray();
	}
	return result.messages[0];
}

PackedByteArray GrpcClient::invoke_server_stream(const String &p_method, const PackedByteArray &p_request,
		const String &p_metadata_json, int p_timeout_ms) {
	String metadata_error;
	Vector<grpc::Header> metadata = grpc::metadata_from_json(p_metadata_json, metadata_error);
	if (!metadata_error.is_empty()) {
		grpc::CallResult error_result;
		error_result.status = grpc::Status(grpc::STATUS_INVALID_ARGUMENT, metadata_error);
		last_result = error_result;
		return PackedByteArray();
	}
	grpc::CallResult result = _run(p_method, p_request, metadata, p_timeout_ms);
	return grpc::frame_messages(result.messages);
}

bool GrpcClient::_ensure_method(const String &p_method, const grpc::GrpcMethod **r_method, const grpc::GrpcDescriptor **r_input,
		const grpc::GrpcDescriptor **r_output, String &r_error) {
	String service_name;
	String method_name;
	if (!split_method(p_method, service_name, method_name)) {
		r_error = "gRPC method must be /Service/Method, got " + p_method;
		return false;
	}

	if (!registry.find_service(service_name)) {
		if (!_reflection_fetch(service_name, r_error)) {
			return false;
		}
	}

	const grpc::GrpcService *service = registry.find_service(service_name);
	if (!service) {
		r_error = "service " + service_name + " not found";
		return false;
	}
	const grpc::GrpcMethod *method = service->find_method(method_name);
	if (!method) {
		r_error = service_name + " has no method " + method_name;
		return false;
	}
	const grpc::GrpcDescriptor *input = registry.find_message(method->input_type);
	const grpc::GrpcDescriptor *output = registry.find_message(method->output_type);
	if (!input || !output) {
		r_error = "missing descriptors for " + service_name + "/" + method_name;
		return false;
	}
	*r_method = method;
	*r_input = input;
	*r_output = output;
	return true;
}

bool GrpcClient::_reflection_fetch(const String &p_service, String &r_error) {
	Vector<uint8_t> request;
	{
		grpc::ProtoWriter writer(request);
		writer.string_field(4, p_service); // ServerReflectionRequest.file_containing_symbol
	}

	const char *versions[] = { "v1alpha", "v1" };
	String last_error;

	for (const char *version : versions) {
		String method = String("/grpc.reflection.") + version + ".ServerReflection/ServerReflectionInfo";
		grpc::H2Session session;
		Error err = session.start(host, port, use_tls, server_name, method, Vector<grpc::Header>(), 8000);
		if (err != OK) {
			last_error = session.get_result().status.message;
			continue;
		}

		session.send_message(request);

		Vector<Vector<uint8_t>> messages;
		while (!session.is_finished() && messages.is_empty()) {
			Error pump_err = session.pump(50);
			Vector<Vector<uint8_t>> received = session.take_messages();
			if (!received.is_empty()) {
				messages.append_array(received);
			}
			if (pump_err != OK) {
				break;
			}
		}

		grpc::CallResult result = session.get_result();
		if (!messages.is_empty()) {
			bool parsed_any = false;
			bool error_response = false;
			String server_error;

			for (const Vector<uint8_t> &message : messages) {
				grpc::ProtoReader reader(message.ptr(), message.size());
				while (!reader.eof()) {
					int field = 0;
					int wire = 0;
					if (!reader.read_tag(field, wire)) {
						break;
					}
					if (field == 4 && wire == grpc::WIRE_LENGTH) {
						const uint8_t *data = nullptr;
						int length = 0;
						if (!reader.read_bytes(data, length)) {
							break;
						}
						grpc::ProtoReader response_reader(data, length);
						while (!response_reader.eof()) {
							int response_field = 0;
							int response_wire = 0;
							if (!response_reader.read_tag(response_field, response_wire)) {
								break;
							}
							if (response_field == 1 && response_wire == grpc::WIRE_LENGTH) {
								const uint8_t *descriptor_data = nullptr;
								int descriptor_length = 0;
								if (!response_reader.read_bytes(descriptor_data, descriptor_length)) {
									break;
								}
								Vector<uint8_t> descriptor;
								descriptor.resize(descriptor_length);
								if (descriptor_length > 0) {
									memcpy(descriptor.ptrw(), descriptor_data, descriptor_length);
								}
								if (registry.parse_file(descriptor)) {
									parsed_any = true;
								}
							} else if (!response_reader.skip(response_wire)) {
								break;
							}
						}
					} else if (field == 7 && wire == grpc::WIRE_LENGTH) {
						const uint8_t *data = nullptr;
						int length = 0;
						if (!reader.read_bytes(data, length)) {
							break;
						}
						error_response = true;
						grpc::ProtoReader error_reader(data, length);
						while (!error_reader.eof()) {
							int error_field = 0;
							int error_wire = 0;
							if (!error_reader.read_tag(error_field, error_wire)) {
								break;
							}
							if (error_field == 2 && error_wire == grpc::WIRE_LENGTH) {
								const uint8_t *text = nullptr;
								int text_length = 0;
								if (error_reader.read_bytes(text, text_length)) {
									server_error = String::utf8((const char *)text, text_length);
								}
							} else if (!error_reader.skip(error_wire)) {
								break;
							}
						}
					} else if (!reader.skip(wire)) {
						break;
					}
				}
			}

			if (error_response && !parsed_any) {
				last_error = server_error.is_empty() ? "reflection error" : server_error;
				session.close_send();
				continue;
			}

			if (parsed_any) {
				registry.resolve();
				session.close_send();
				if (registry.find_service(p_service)) {
					return true;
				}
				last_error = "reflection did not return service " + p_service;
				continue;
			}
		}

		if (!result.status.ok()) {
			last_error = result.status.message;
		}
		session.close_send();
		if (!last_error.is_empty()) {
			continue;
		}
	}

	r_error = last_error.is_empty() ? "gRPC reflection is not available" : last_error;
	return false;
}

String GrpcClient::invoke_json(const String &p_method, const String &p_request_json, const String &p_metadata_json, int p_timeout_ms) {
	const grpc::GrpcMethod *method = nullptr;
	const grpc::GrpcDescriptor *input = nullptr;
	const grpc::GrpcDescriptor *output = nullptr;
	String descriptor_error;
	if (!_ensure_method(p_method, &method, &input, &output, descriptor_error)) {
		return _error_json(descriptor_error);
	}

	Variant parsed = JSON::parse_string(p_request_json);
	if (parsed.get_type() == Variant::NIL && !p_request_json.strip_edges().is_empty()) {
		return _error_json("invalid JSON request for " + p_method);
	}

	Vector<uint8_t> request;
	String encode_error;
	if (!grpc::ProtoJson::encode_message(registry, *input, parsed, request, encode_error)) {
		return _error_json("could not encode request: " + encode_error);
	}

	String metadata_error;
	Vector<grpc::Header> metadata = grpc::metadata_from_json(p_metadata_json, metadata_error);
	if (!metadata_error.is_empty()) {
		return _error_json(metadata_error);
	}

	grpc::CallResult result = _run(p_method, request, metadata, p_timeout_ms);
	if (!result.status.ok()) {
		return _error_json(result);
	}
	if (result.messages.is_empty()) {
		return _error_json("server returned no message");
	}

	Variant decoded;
	String decode_error;
	if (!grpc::ProtoJson::decode_message(registry, *output, result.messages[0].ptr(), result.messages[0].size(), decoded, decode_error)) {
		return _error_json("could not decode response: " + decode_error);
	}

	Dictionary out;
	out["ok"] = true;
	out["data"] = decoded;
	return JSON::stringify(out);
}

String GrpcClient::invoke_server_stream_json(const String &p_method, const String &p_request_json, const String &p_metadata_json,
		int p_timeout_ms) {
	const grpc::GrpcMethod *method = nullptr;
	const grpc::GrpcDescriptor *input = nullptr;
	const grpc::GrpcDescriptor *output = nullptr;
	String descriptor_error;
	if (!_ensure_method(p_method, &method, &input, &output, descriptor_error)) {
		return _error_json(descriptor_error);
	}

	Variant parsed = JSON::parse_string(p_request_json);
	if (parsed.get_type() == Variant::NIL && !p_request_json.strip_edges().is_empty()) {
		return _error_json("invalid JSON request for " + p_method);
	}

	Vector<uint8_t> request;
	String encode_error;
	if (!grpc::ProtoJson::encode_message(registry, *input, parsed, request, encode_error)) {
		return _error_json("could not encode request: " + encode_error);
	}

	String metadata_error;
	Vector<grpc::Header> metadata = grpc::metadata_from_json(p_metadata_json, metadata_error);
	if (!metadata_error.is_empty()) {
		return _error_json(metadata_error);
	}

	grpc::CallResult result = _run(p_method, request, metadata, p_timeout_ms);
	if (!result.status.ok()) {
		return _error_json(result);
	}

	Array messages;
	for (const Vector<uint8_t> &message : result.messages) {
		Variant decoded;
		String decode_error;
		if (!grpc::ProtoJson::decode_message(registry, *output, message.ptr(), message.size(), decoded, decode_error)) {
			return _error_json("could not decode response: " + decode_error);
		}
		messages.push_back(decoded);
	}

	Dictionary out;
	out["ok"] = true;
	out["data"] = messages;
	return JSON::stringify(out);
}

void GrpcClient::_prune_active() {
	for (int i = active_calls.size() - 1; i >= 0; i--) {
		if (active_calls[i].is_valid() && active_calls[i]->is_finished()) {
			active_calls.remove_at(i);
		}
	}
	for (int i = active_streams.size() - 1; i >= 0; i--) {
		if (active_streams[i].is_valid() && active_streams[i]->is_closed()) {
			active_streams.remove_at(i);
		}
	}
}

Ref<GrpcCall> GrpcClient::call_async(const String &p_method, const PackedByteArray &p_request, const Dictionary &p_metadata,
		int p_timeout_ms, bool p_server_streaming) {
	_prune_active();
	Ref<GrpcCall> grpc_call;
	grpc_call.instantiate();
	String metadata_error;
	Vector<grpc::Header> metadata = grpc::metadata_from_dict(p_metadata, metadata_error);
	grpc_call->configure(host, port, use_tls, server_name, normalize_method(p_method), p_request, metadata, p_timeout_ms,
			p_server_streaming);
	if (grpc_call->start() != OK) {
		return Ref<GrpcCall>();
	}
	active_calls.push_back(grpc_call);
	return grpc_call;
}

Ref<GrpcStream> GrpcClient::open_stream(const String &p_method, const PackedByteArray &p_request, const Dictionary &p_metadata,
		int p_timeout_ms) {
	_prune_active();
	Ref<GrpcStream> stream;
	stream.instantiate();
	String metadata_error;
	Vector<grpc::Header> metadata = grpc::metadata_from_dict(p_metadata, metadata_error);
	stream->configure(host, port, use_tls, server_name, normalize_method(p_method), metadata, p_timeout_ms);
	if (stream->start() != OK) {
		return Ref<GrpcStream>();
	}
	if (!p_request.is_empty()) {
		stream->send(p_request);
	}
	active_streams.push_back(stream);
	return stream;
}
