/**************************************************************************/
/*  grpc_types.h                                                          */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#pragma once

#include "core/string/ustring.h"
#include "core/templates/vector.h"
#include "core/variant/variant.h"

namespace grpc {

// gRPC status codes (https://github.com/grpc/grpc/blob/master/doc/statuscodes.md).
enum StatusCode {
	STATUS_OK = 0,
	STATUS_CANCELLED = 1,
	STATUS_UNKNOWN = 2,
	STATUS_INVALID_ARGUMENT = 3,
	STATUS_DEADLINE_EXCEEDED = 4,
	STATUS_NOT_FOUND = 5,
	STATUS_ALREADY_EXISTS = 6,
	STATUS_PERMISSION_DENIED = 7,
	STATUS_RESOURCE_EXHAUSTED = 8,
	STATUS_FAILED_PRECONDITION = 9,
	STATUS_ABORTED = 10,
	STATUS_OUT_OF_RANGE = 11,
	STATUS_UNIMPLEMENTED = 12,
	STATUS_INTERNAL = 13,
	STATUS_UNAVAILABLE = 14,
	STATUS_DATA_LOSS = 15,
	STATUS_UNAUTHENTICATED = 16,
};

struct Header {
	String name;
	String value;

	Header() = default;
	Header(const String &p_name, const String &p_value) :
			name(p_name), value(p_value) {}
};

struct Status {
	int code = STATUS_UNKNOWN;
	String message;

	Status() = default;
	Status(int p_code, const String &p_message) :
			code(p_code), message(p_message) {}

	bool ok() const { return code == STATUS_OK; }
};

struct CallResult {
	Status status;
	int http_status = 0;
	Vector<Header> headers;
	Vector<Header> trailers;
	Vector<Vector<uint8_t>> messages;

	void add_header(const String &p_name, const String &p_value, bool p_trailer) {
		Header h(p_name, p_value);
		if (p_trailer) {
			trailers.push_back(h);
		} else {
			headers.push_back(h);
		}
	}

	Dictionary headers_to_dict() const {
		Dictionary out;
		for (const Header &h : headers) {
			Array values;
			if (out.has(h.name)) {
				values = out[h.name];
			}
			values.push_back(h.value);
			out[h.name] = values;
		}
		return out;
	}

	Dictionary trailers_to_dict() const {
		Dictionary out;
		for (const Header &h : trailers) {
			Array values;
			if (out.has(h.name)) {
				values = out[h.name];
			}
			values.push_back(h.value);
			out[h.name] = values;
		}
		return out;
	}
};

inline String status_name(int p_code) {
	switch (p_code) {
		case STATUS_OK:
			return "OK";
		case STATUS_CANCELLED:
			return "CANCELLED";
		case STATUS_UNKNOWN:
			return "UNKNOWN";
		case STATUS_INVALID_ARGUMENT:
			return "INVALID_ARGUMENT";
		case STATUS_DEADLINE_EXCEEDED:
			return "DEADLINE_EXCEEDED";
		case STATUS_NOT_FOUND:
			return "NOT_FOUND";
		case STATUS_ALREADY_EXISTS:
			return "ALREADY_EXISTS";
		case STATUS_PERMISSION_DENIED:
			return "PERMISSION_DENIED";
		case STATUS_RESOURCE_EXHAUSTED:
			return "RESOURCE_EXHAUSTED";
		case STATUS_FAILED_PRECONDITION:
			return "FAILED_PRECONDITION";
		case STATUS_ABORTED:
			return "ABORTED";
		case STATUS_OUT_OF_RANGE:
			return "OUT_OF_RANGE";
		case STATUS_UNIMPLEMENTED:
			return "UNIMPLEMENTED";
		case STATUS_INTERNAL:
			return "INTERNAL";
		case STATUS_UNAVAILABLE:
			return "UNAVAILABLE";
		case STATUS_DATA_LOSS:
			return "DATA_LOSS";
		case STATUS_UNAUTHENTICATED:
			return "UNAUTHENTICATED";
		default:
			return "UNKNOWN";
	}
}

// gRPC percent-encodes grpc-message; decode the common case.
inline String decode_grpc_message(const String &p_value) {
	String out;
	int len = p_value.length();
	for (int i = 0; i < len; i++) {
		char32_t c = p_value[i];
		if (c == '%' && i + 2 < len) {
			int hi = 0;
			int lo = 0;
			char32_t a = p_value[i + 1];
			char32_t b = p_value[i + 2];
			if (a >= '0' && a <= '9') {
				hi = a - '0';
			} else if (a >= 'A' && a <= 'F') {
				hi = a - 'A' + 10;
			} else if (a >= 'a' && a <= 'f') {
				hi = a - 'a' + 10;
			} else {
				out += c;
				continue;
			}
			if (b >= '0' && b <= '9') {
				lo = b - '0';
			} else if (b >= 'A' && b <= 'F') {
				lo = b - 'A' + 10;
			} else if (b >= 'a' && b <= 'f') {
				lo = b - 'a' + 10;
			} else {
				out += c;
				continue;
			}
			out += String::chr(hi * 16 + lo);
			i += 2;
		} else {
			out += c;
		}
	}
	return out;
}

inline String encode_grpc_message(const String &p_message) {
	String out;
	for (int i = 0; i < p_message.length(); i++) {
		char32_t c = p_message[i];
		if (c < 0x20 || c > 0x7e || c == '%') {
			out += "%" + String::num_int64(c, 16).to_upper().pad_zeros(2);
		} else {
			out += c;
		}
	}
	return out;
}

// grpc-timeout is 1-8 digits followed by H, M, S, m, u or n.
inline String timeout_header(int p_timeout_ms) {
	if (p_timeout_ms <= 0) {
		return String();
	}
	// Use the largest unit that keeps the value below 8 digits.
	if (p_timeout_ms % 1000 == 0 && p_timeout_ms / 1000 < 100000000) {
		int seconds = p_timeout_ms / 1000;
		if (seconds > 0) {
			return itos(seconds) + "S";
		}
	}
	if (p_timeout_ms < 100000000) {
		return itos(p_timeout_ms) + "m";
	}
	return itos(MAX(1, p_timeout_ms / 1000)) + "S";
}

// Maps a non-200 HTTP status to a gRPC status per the gRPC HTTP/2 spec.
inline int http_status_to_grpc(int p_http_status) {
	switch (p_http_status) {
		case 400:
			return STATUS_INTERNAL;
		case 401:
			return STATUS_UNAUTHENTICATED;
		case 403:
			return STATUS_PERMISSION_DENIED;
		case 404:
			return STATUS_UNIMPLEMENTED;
		case 429:
		case 502:
		case 503:
		case 504:
			return STATUS_UNAVAILABLE;
		default:
			return STATUS_UNKNOWN;
	}
}

} // namespace grpc
