/**************************************************************************/
/*  wasi_host.cpp                                                         */
/**************************************************************************/

#include "wasi_host.h"

#include "core/io/json.h"
#include "core/object/class_db.h"

#include <cstdint>

// gok's wazero host module. The guest sends a JSON request
// {"method","path","headers","body"} and receives {"status","body"|"error"}.
extern "C" uint32_t gok_http_request(uint32_t p_req_ptr, uint32_t p_req_len, uint32_t p_out_ptr, uint32_t p_out_cap)
		__attribute__((__import_module__("gok"), __import_name__("http_request")));

void WASIHost::_bind_methods() {
	ClassDB::bind_static_method("WASIHost", D_METHOD("fetch_json", "method", "path", "headers", "body"), &WASIHost::fetch_json, DEFVAL(Dictionary()), DEFVAL(String()));
}

Dictionary WASIHost::fetch_json(const String &p_method, const String &p_path, const Dictionary &p_headers, const String &p_body) {
	Dictionary request;
	request["method"] = p_method;
	request["path"] = p_path;
	request["headers"] = p_headers;
	request["body"] = p_body;

	CharString request_utf8 = JSON::stringify(request).utf8();
	PackedByteArray out;
	out.resize(1 << 20);
	uint32_t result = gok_http_request(
			(uint32_t)(uintptr_t)request_utf8.get_data(),
			(uint32_t)request_utf8.length(),
			(uint32_t)(uintptr_t)out.ptrw(),
			(uint32_t)out.size());
	if (result == 0) {
		Dictionary error;
		error["ok"] = false;
		error["error"] = "gok host bridge is not available";
		return error;
	}
	if (result & 0x80000000u) {
		uint32_t needed = result & 0x7fffffffu;
		out.resize(needed);
		result = gok_http_request(
				(uint32_t)(uintptr_t)request_utf8.get_data(),
				(uint32_t)request_utf8.length(),
				(uint32_t)(uintptr_t)out.ptrw(),
				(uint32_t)out.size());
		if ((result & 0x80000000u) || result == 0) {
			Dictionary error;
			error["ok"] = false;
			error["error"] = "gok host bridge response too large";
			return error;
		}
	}

	Variant parsed = JSON::parse_string(String::utf8((const char *)out.ptr(), result));
	if (parsed.get_type() != Variant::DICTIONARY) {
		Dictionary error;
		error["ok"] = false;
		error["error"] = "gok host bridge returned invalid JSON";
		return error;
	}
	Dictionary response = parsed;
	Dictionary result_dict;
	int status = response.has("status") ? int(response["status"]) : 0;
	String body = response.has("body") ? String(response["body"]) : String();
	String error = response.has("error") ? String(response["error"]) : String();
	result_dict["status"] = status;
	result_dict["body"] = body;
	if (!error.is_empty()) {
		result_dict["error"] = error;
	}
	String trimmed = body.strip_edges();
	if (trimmed.begins_with("{") || trimmed.begins_with("[")) {
		Variant json = JSON::parse_string(trimmed);
		if (json.get_type() != Variant::NIL) {
			result_dict["json"] = json;
		}
	}
	result_dict["ok"] = status >= 200 && status < 300;
	return result_dict;
}
