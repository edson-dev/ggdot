/**************************************************************************/
/*  grpc_binary.h                                                         */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#pragma once

#include "core/string/ustring.h"
#include "core/templates/vector.h"
#include "core/typedefs.h"

#include <cstring>

namespace grpc {

// Protobuf wire types.
enum WireType {
	WIRE_VARINT = 0,
	WIRE_FIXED64 = 1,
	WIRE_LENGTH = 2,
	WIRE_START_GROUP = 3,
	WIRE_END_GROUP = 4,
	WIRE_FIXED32 = 5,
};

// Protobuf field types (descriptor.proto FieldDescriptorProto.Type).
enum FieldType {
	TYPE_DOUBLE = 1,
	TYPE_FLOAT = 2,
	TYPE_INT64 = 3,
	TYPE_UINT64 = 4,
	TYPE_INT32 = 5,
	TYPE_FIXED64 = 6,
	TYPE_FIXED32 = 7,
	TYPE_BOOL = 8,
	TYPE_STRING = 9,
	TYPE_GROUP = 10,
	TYPE_MESSAGE = 11,
	TYPE_BYTES = 12,
	TYPE_UINT32 = 13,
	TYPE_ENUM = 14,
	TYPE_SFIXED32 = 15,
	TYPE_SFIXED64 = 16,
	TYPE_SINT32 = 17,
	TYPE_SINT64 = 18,
};

enum FieldLabel {
	LABEL_OPTIONAL = 1,
	LABEL_REQUIRED = 2,
	LABEL_REPEATED = 3,
};

class ProtoReader {
	const uint8_t *data = nullptr;
	int size = 0;
	int pos = 0;

public:
	ProtoReader(const uint8_t *p_data, int p_size) :
			data(p_data), size(p_size) {}

	bool eof() const { return pos >= size; }
	int position() const { return pos; }
	int remaining() const { return size - pos; }
	const uint8_t *ptr() const { return data + pos; }
	void seek(int p_pos) { pos = p_pos; }

	bool read_varint(uint64_t &r_value) {
		uint64_t result = 0;
		int shift = 0;
		while (pos < size && shift < 64) {
			uint8_t byte = data[pos++];
			result |= (uint64_t)(byte & 0x7f) << shift;
			if ((byte & 0x80) == 0) {
				r_value = result;
				return true;
			}
			shift += 7;
		}
		return false;
	}

	bool read_tag(int &r_field, int &r_wire) {
		uint64_t key = 0;
		if (!read_varint(key)) {
			return false;
		}
		r_field = (int)(key >> 3);
		r_wire = (int)(key & 7);
		return r_field > 0;
	}

	bool read_bytes(const uint8_t *&r_data, int &r_length) {
		uint64_t length = 0;
		if (!read_varint(length)) {
			return false;
		}
		if (length > (uint64_t)(size - pos)) {
			return false;
		}
		r_data = data + pos;
		r_length = (int)length;
		pos += (int)length;
		return true;
	}

	bool read_fixed32(uint32_t &r_value) {
		if (size - pos < 4) {
			return false;
		}
		r_value = (uint32_t)data[pos] | ((uint32_t)data[pos + 1] << 8) | ((uint32_t)data[pos + 2] << 16) |
				((uint32_t)data[pos + 3] << 24);
		pos += 4;
		return true;
	}

	bool read_fixed64(uint64_t &r_value) {
		if (size - pos < 8) {
			return false;
		}
		r_value = 0;
		for (int i = 0; i < 8; i++) {
			r_value |= (uint64_t)data[pos + i] << (8 * i);
		}
		pos += 8;
		return true;
	}

	bool skip(int p_wire) {
		switch (p_wire) {
			case WIRE_VARINT: {
				uint64_t ignored = 0;
				return read_varint(ignored);
			}
			case WIRE_FIXED64: {
				uint64_t ignored = 0;
				return read_fixed64(ignored);
			}
			case WIRE_LENGTH: {
				const uint8_t *ignored = nullptr;
				int length = 0;
				return read_bytes(ignored, length);
			}
			case WIRE_FIXED32: {
				uint32_t ignored = 0;
				return read_fixed32(ignored);
			}
			case WIRE_START_GROUP: {
				while (!eof()) {
					int field = 0;
					int wire = 0;
					if (!read_tag(field, wire)) {
						return false;
					}
					if (wire == WIRE_END_GROUP) {
						return true;
					}
					if (!skip(wire)) {
						return false;
					}
				}
				return false;
			}
			default:
				return false;
		}
	}
};

class ProtoWriter {
	Vector<uint8_t> &out;

public:
	ProtoWriter(Vector<uint8_t> &p_out) :
			out(p_out) {}

	void varint(uint64_t p_value) {
		while (p_value >= 0x80) {
			out.push_back((uint8_t)(p_value & 0x7f) | 0x80);
			p_value >>= 7;
		}
		out.push_back((uint8_t)p_value);
	}

	void tag(int p_field, int p_wire) {
		varint(((uint64_t)p_field << 3) | (uint64_t)p_wire);
	}

	void bytes_field(int p_field, const uint8_t *p_data, int p_length) {
		tag(p_field, WIRE_LENGTH);
		varint((uint64_t)p_length);
		for (int i = 0; i < p_length; i++) {
			out.push_back(p_data[i]);
		}
	}

	void bytes_field(int p_field, const Vector<uint8_t> &p_data) {
		bytes_field(p_field, p_data.ptr(), p_data.size());
	}

	void string_field(int p_field, const String &p_value) {
		Vector<uint8_t> utf8 = p_value.to_utf8_buffer();
		bytes_field(p_field, utf8.ptr(), utf8.size());
	}

	void message_field(int p_field, const Vector<uint8_t> &p_data) {
		bytes_field(p_field, p_data);
	}

	void varint_field(int p_field, uint64_t p_value) {
		tag(p_field, WIRE_VARINT);
		varint(p_value);
	}

	void fixed32_field(int p_field, uint32_t p_value) {
		tag(p_field, WIRE_FIXED32);
		out.push_back(p_value & 0xff);
		out.push_back((p_value >> 8) & 0xff);
		out.push_back((p_value >> 16) & 0xff);
		out.push_back((p_value >> 24) & 0xff);
	}

	void fixed64_field(int p_field, uint64_t p_value) {
		tag(p_field, WIRE_FIXED64);
		for (int i = 0; i < 8; i++) {
			out.push_back((p_value >> (8 * i)) & 0xff);
		}
	}
};

inline uint64_t zigzag_encode(int64_t p_value) {
	return ((uint64_t)p_value << 1) ^ (uint64_t)(p_value >> 63);
}

inline int64_t zigzag_decode(uint64_t p_value) {
	return (int64_t)((p_value >> 1) ^ (~(p_value & 1) + 1));
}

} // namespace grpc
