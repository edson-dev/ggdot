/**************************************************************************/
/*  grpc_util.cpp                                                         */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#include "grpc_util.h"

#include "core/io/json.h"

namespace grpc {

static String base64_encode_metadata(const uint8_t *p_data, int p_length) {
	static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	String out;
	for (int i = 0; i < p_length; i += 3) {
		uint32_t chunk = (uint32_t)p_data[i] << 16;
		if (i + 1 < p_length) {
			chunk |= (uint32_t)p_data[i + 1] << 8;
		}
		if (i + 2 < p_length) {
			chunk |= (uint32_t)p_data[i + 2];
		}
		out += alphabet[(chunk >> 18) & 0x3f];
		out += alphabet[(chunk >> 12) & 0x3f];
		out += (i + 1 < p_length) ? alphabet[(chunk >> 6) & 0x3f] : '=';
		out += (i + 2 < p_length) ? alphabet[chunk & 0x3f] : '=';
	}
	return out;
}

Vector<Header> metadata_from_dict(const Dictionary &p_metadata, String &r_error) {
	Vector<Header> out;
	for (const KeyValue<Variant, Variant> &entry : p_metadata) {
		String key = ((String)entry.key).to_lower();
		if (key.is_empty()) {
			continue;
		}
		const Variant &value = entry.value;
		if (value.get_type() == Variant::ARRAY) {
			const Array array = value;
			for (int i = 0; i < array.size(); i++) {
				out.push_back(Header(key, (String)array[i]));
			}
		} else if (value.get_type() == Variant::PACKED_BYTE_ARRAY) {
			PackedByteArray raw = value;
			out.push_back(Header(key, base64_encode_metadata(raw.ptr(), raw.size())));
		} else if (value.get_type() != Variant::NIL) {
			out.push_back(Header(key, (String)value));
		}
	}
	(void)r_error;
	return out;
}

Vector<Header> metadata_from_json(const String &p_json, String &r_error) {
	if (p_json.strip_edges().is_empty()) {
		return Vector<Header>();
	}
	Variant parsed = JSON::parse_string(p_json);
	if (parsed.get_type() != Variant::DICTIONARY) {
		r_error = "metadata must be a JSON object";
		return Vector<Header>();
	}
	return metadata_from_dict(parsed, r_error);
}

Dictionary status_to_dict(const CallResult &p_result) {
	Dictionary out;
	out["ok"] = p_result.status.ok();
	out["code"] = p_result.status.code;
	out["message"] = p_result.status.message;
	out["name"] = status_name(p_result.status.code);
	out["http_status"] = p_result.http_status;
	out["headers"] = p_result.headers_to_dict();
	out["trailers"] = p_result.trailers_to_dict();
	return out;
}

String status_to_json(const CallResult &p_result) {
	return JSON::stringify(status_to_dict(p_result));
}

Vector<uint8_t> frame_messages(const Vector<Vector<uint8_t>> &p_messages) {
	Vector<uint8_t> out;
	for (const Vector<uint8_t> &message : p_messages) {
		uint32_t size = message.size();
		out.push_back((size >> 24) & 0xff);
		out.push_back((size >> 16) & 0xff);
		out.push_back((size >> 8) & 0xff);
		out.push_back(size & 0xff);
		out.append_array(message);
	}
	return out;
}

} // namespace grpc
