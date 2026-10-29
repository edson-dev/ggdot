/**************************************************************************/
/*  grpc_json.cpp                                                         */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#include "grpc_json.h"

#include <cstring>

namespace grpc {

static const char *WKT_STRUCT = "google.protobuf.Struct";
static const char *WKT_VALUE = "google.protobuf.Value";
static const char *WKT_LIST_VALUE = "google.protobuf.ListValue";
static const char *WKT_EMPTY = "google.protobuf.Empty";

static bool is_well_known(const String &p_name) {
	return p_name == WKT_STRUCT || p_name == WKT_VALUE || p_name == WKT_LIST_VALUE || p_name == WKT_EMPTY;
}

static bool packable(int p_type) {
	switch (p_type) {
		case TYPE_STRING:
		case TYPE_BYTES:
		case TYPE_MESSAGE:
		case TYPE_GROUP:
			return false;
		default:
			return true;
	}
}

static String base64_encode(const uint8_t *p_data, int p_length) {
	static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	String out;
	out.reserve(((p_length + 2) / 3) * 4);
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

static int base64_value(char32_t p_char) {
	if (p_char >= 'A' && p_char <= 'Z') {
		return p_char - 'A';
	}
	if (p_char >= 'a' && p_char <= 'z') {
		return p_char - 'a' + 26;
	}
	if (p_char >= '0' && p_char <= '9') {
		return p_char - '0' + 52;
	}
	if (p_char == '+' || p_char == '-') {
		return 62;
	}
	if (p_char == '/' || p_char == '_') {
		return 63;
	}
	return -1;
}

static bool base64_decode(const String &p_text, Vector<uint8_t> &r_out) {
	int value = 0;
	int bits = -8;
	for (int i = 0; i < p_text.length(); i++) {
		char32_t c = p_text[i];
		if (c == '=' || c == ' ' || c == '\n' || c == '\r') {
			continue;
		}
		int digit = base64_value(c);
		if (digit < 0) {
			return false;
		}
		value = (value << 6) | digit;
		bits += 6;
		if (bits >= 0) {
			r_out.push_back((uint8_t)((value >> bits) & 0xff));
			bits -= 8;
		}
	}
	return true;
}

static bool is_default_value(const Variant &p_value, int p_type) {
	if (p_value.get_type() == Variant::NIL) {
		return true;
	}
	switch (p_type) {
		case TYPE_BOOL:
			return !(bool)p_value;
		case TYPE_STRING:
			return p_value.get_type() == Variant::STRING && ((String)p_value).is_empty();
		case TYPE_BYTES:
			return p_value.get_type() == Variant::PACKED_BYTE_ARRAY && ((PackedByteArray)p_value).is_empty();
		case TYPE_ENUM:
			if (p_value.get_type() == Variant::STRING) {
				// Enum names are never the implicit zero default unless the name is
				// the zero value itself; callers pass resolved numbers in practice.
				return false;
			}
			return (int64_t)p_value == 0;
		case TYPE_MESSAGE:
		case TYPE_GROUP:
			return false;
		default:
			return (double)p_value == 0.0;
	}
}

static uint64_t double_bits(double p_value) {
	uint64_t bits = 0;
	memcpy(&bits, &p_value, sizeof(bits));
	return bits;
}

static double bits_double(uint64_t p_value) {
	double value = 0;
	memcpy(&value, &p_value, sizeof(value));
	return value;
}

static uint32_t float_bits(float p_value) {
	uint32_t bits = 0;
	memcpy(&bits, &p_value, sizeof(bits));
	return bits;
}

static float bits_float(uint32_t p_value) {
	float value = 0;
	memcpy(&value, &p_value, sizeof(value));
	return value;
}

static bool encode_well_known(const GrpcRegistry &p_registry, const String &p_type_name, const Variant &p_json,
		Vector<uint8_t> &r_out, String &r_error);

static bool encode_field_value(const GrpcRegistry &p_registry, const GrpcField &p_field, const Variant &p_value,
		Vector<uint8_t> &r_out, String &r_error) {
	const int number = p_field.number;
	switch (p_field.type) {
		case TYPE_STRING: {
			ProtoWriter writer(r_out);
			writer.string_field(number, (String)p_value);
			return true;
		}
		case TYPE_BYTES: {
			Vector<uint8_t> raw;
			if (p_value.get_type() == Variant::PACKED_BYTE_ARRAY) {
				raw = (PackedByteArray)p_value;
			} else if (!base64_decode((String)p_value, raw)) {
				r_error = "invalid base64 for bytes field " + p_field.name;
				return false;
			}
			ProtoWriter writer(r_out);
			writer.bytes_field(number, raw);
			return true;
		}
		case TYPE_BOOL: {
			ProtoWriter writer(r_out);
			writer.varint_field(number, (bool)p_value ? 1 : 0);
			return true;
		}
		case TYPE_ENUM: {
			int64_t value = 0;
			if (p_value.get_type() == Variant::STRING && p_field.enum_type) {
				String name = p_value;
				bool found = false;
				for (const GrpcEnumValue &candidate : p_field.enum_type->values) {
					if (candidate.name == name) {
						value = candidate.number;
						found = true;
						break;
					}
				}
				if (!found) {
					r_error = "unknown enum value " + name + " for " + p_field.name;
					return false;
				}
			} else {
				value = (int64_t)p_value;
			}
			ProtoWriter writer(r_out);
			writer.varint_field(number, (uint64_t)value);
			return true;
		}
		case TYPE_INT64:
		case TYPE_INT32:
		case TYPE_UINT64:
		case TYPE_UINT32: {
			int64_t signed_value = (int64_t)p_value;
			uint64_t encoded = (p_field.type == TYPE_INT32 || p_field.type == TYPE_INT64) ? (uint64_t)signed_value
																						 : (uint64_t)signed_value;
			if (p_field.type == TYPE_INT32) {
				encoded = (uint64_t)(int64_t)(int32_t)signed_value;
			}
			ProtoWriter writer(r_out);
			writer.varint_field(number, encoded);
			return true;
		}
		case TYPE_SINT32:
		case TYPE_SINT64: {
			ProtoWriter writer(r_out);
			writer.varint_field(number, zigzag_encode((int64_t)p_value));
			return true;
		}
		case TYPE_FIXED32:
		case TYPE_SFIXED32: {
			ProtoWriter writer(r_out);
			writer.fixed32_field(number, (uint32_t)(int64_t)p_value);
			return true;
		}
		case TYPE_FLOAT: {
			ProtoWriter writer(r_out);
			writer.fixed32_field(number, float_bits((float)(double)p_value));
			return true;
		}
		case TYPE_FIXED64:
		case TYPE_SFIXED64: {
			ProtoWriter writer(r_out);
			writer.fixed64_field(number, (uint64_t)(int64_t)p_value);
			return true;
		}
		case TYPE_DOUBLE: {
			ProtoWriter writer(r_out);
			writer.fixed64_field(number, double_bits((double)p_value));
			return true;
		}
		case TYPE_MESSAGE:
		case TYPE_GROUP: {
			Vector<uint8_t> nested;
			if (is_well_known(p_field.type_name)) {
				if (!encode_well_known(p_registry, p_field.type_name, p_value, nested, r_error)) {
					return false;
				}
			} else {
				const GrpcDescriptor *descriptor = p_field.message_type;
				if (!descriptor) {
					r_error = "unknown message type " + p_field.type_name;
					return false;
				}
				if (!ProtoJson::encode_message(p_registry, *descriptor, p_value, nested, r_error)) {
					return false;
				}
			}
			ProtoWriter writer(r_out);
			writer.message_field(number, nested);
			return true;
		}
		default:
			r_error = "unsupported protobuf field type";
			return false;
	}
}

static bool encode_map_entry(const GrpcRegistry &p_registry, const GrpcField &p_field, const Variant &p_key,
		const Variant &p_value, Vector<uint8_t> &r_out, String &r_error) {
	const GrpcDescriptor *entry = p_field.map_entry;
	if (!entry) {
		r_error = "map field without entry descriptor";
		return false;
	}
	const GrpcField *key_field = entry->field_by_number(1);
	const GrpcField *value_field = entry->field_by_number(2);
	if (!key_field || !value_field) {
		r_error = "invalid map entry descriptor";
		return false;
	}

	Vector<uint8_t> key_bytes;
	Vector<uint8_t> value_bytes;

	Variant key = p_key;
	if (key_field->type != TYPE_STRING) {
		// JSON object keys are strings; convert numeric/bool keys.
		if (key_field->type == TYPE_BOOL) {
			key = ((String)p_key).to_lower() == "true";
		} else {
			key = ((String)p_key).to_int();
		}
	}
	if (!encode_field_value(p_registry, *key_field, key, key_bytes, r_error)) {
		return false;
	}
	if (value_field->type == TYPE_MESSAGE) {
		Vector<uint8_t> nested;
		if (is_well_known(value_field->type_name)) {
			if (!encode_well_known(p_registry, value_field->type_name, p_value, nested, r_error)) {
				return false;
			}
		} else {
			const GrpcDescriptor *descriptor = value_field->message_type;
			if (!descriptor) {
				r_error = "unknown map value type " + value_field->type_name;
				return false;
			}
			if (!ProtoJson::encode_message(p_registry, *descriptor, p_value, nested, r_error)) {
				return false;
			}
		}
		ProtoWriter writer(value_bytes);
		writer.message_field(value_field->number, nested);
	} else {
		if (!encode_field_value(p_registry, *value_field, p_value, value_bytes, r_error)) {
			return false;
		}
	}

	r_out.append_array(key_bytes);
	r_out.append_array(value_bytes);
	return true;
}

static bool encode_field(const GrpcRegistry &p_registry, const GrpcField &p_field, const Variant &p_value,
		Vector<uint8_t> &r_out, String &r_error) {
	if (p_field.is_map()) {
		if (p_value.get_type() != Variant::DICTIONARY) {
			r_error = "field " + p_field.name + " must be an object";
			return false;
		}
		const Dictionary dict = p_value;
		for (const KeyValue<Variant, Variant> &entry : dict) {
			Vector<uint8_t> entry_bytes;
			if (!encode_map_entry(p_registry, p_field, entry.key, entry.value, entry_bytes, r_error)) {
				return false;
			}
			ProtoWriter writer(r_out);
			writer.message_field(p_field.number, entry_bytes);
		}
		return true;
	}

	if (p_field.repeated()) {
		if (p_value.get_type() != Variant::ARRAY) {
			r_error = "field " + p_field.name + " must be an array";
			return false;
		}
		const Array array = p_value;
		if (array.is_empty()) {
			return true;
		}
		if (packable(p_field.type)) {
			Vector<uint8_t> packed;
			for (int i = 0; i < array.size(); i++) {
				Vector<uint8_t> single;
				if (!encode_field_value(p_registry, p_field, array[i], single, r_error)) {
					return false;
				}
				// encode_field_value writes a tag; strip it and keep the payload.
				ProtoReader reader(single.ptr(), single.size());
				int field = 0;
				int wire = 0;
				if (!reader.read_tag(field, wire)) {
					r_error = "internal packed encoding error";
					return false;
				}
				const uint8_t *payload = reader.ptr();
				int payload_length = reader.remaining();
				for (int j = 0; j < payload_length; j++) {
					packed.push_back(payload[j]);
				}
			}
			if (!packed.is_empty()) {
				ProtoWriter writer(r_out);
				writer.bytes_field(p_field.number, packed);
			}
			return true;
		}
		for (int i = 0; i < array.size(); i++) {
			if (!encode_field_value(p_registry, p_field, array[i], r_out, r_error)) {
				return false;
			}
		}
		return true;
	}

	if (p_field.proto3_optional || p_field.is_message()) {
		return encode_field_value(p_registry, p_field, p_value, r_out, r_error);
	}
	if (is_default_value(p_value, p_field.type)) {
		return true;
	}
	return encode_field_value(p_registry, p_field, p_value, r_out, r_error);
}

static bool encode_value_well_known(const GrpcRegistry &p_registry, const Variant &p_json, Vector<uint8_t> &r_out,
		String &r_error) {
	switch (p_json.get_type()) {
		case Variant::NIL: {
			ProtoWriter writer(r_out);
			writer.varint_field(1, 0);
			return true;
		}
		case Variant::BOOL: {
			ProtoWriter writer(r_out);
			writer.varint_field(4, (bool)p_json ? 1 : 0);
			return true;
		}
		case Variant::INT:
		case Variant::FLOAT: {
			ProtoWriter writer(r_out);
			writer.fixed64_field(2, double_bits((double)p_json));
			return true;
		}
		case Variant::STRING: {
			ProtoWriter writer(r_out);
			writer.string_field(3, p_json);
			return true;
		}
		case Variant::DICTIONARY: {
			Vector<uint8_t> nested;
			if (!encode_well_known(p_registry, WKT_STRUCT, p_json, nested, r_error)) {
				return false;
			}
			ProtoWriter writer(r_out);
			writer.message_field(5, nested);
			return true;
		}
		case Variant::ARRAY: {
			Vector<uint8_t> nested;
			if (!encode_well_known(p_registry, WKT_LIST_VALUE, p_json, nested, r_error)) {
				return false;
			}
			ProtoWriter writer(r_out);
			writer.message_field(6, nested);
			return true;
		}
		default:
			r_error = "unsupported google.protobuf.Value JSON type";
			return false;
	}
}

static bool encode_well_known(const GrpcRegistry &p_registry, const String &p_type_name, const Variant &p_json,
		Vector<uint8_t> &r_out, String &r_error) {
	if (p_type_name == WKT_EMPTY) {
		return true;
	}
	if (p_type_name == WKT_VALUE) {
		return encode_value_well_known(p_registry, p_json, r_out, r_error);
	}
	if (p_type_name == WKT_STRUCT) {
		if (p_json.get_type() != Variant::DICTIONARY) {
			r_error = "google.protobuf.Struct must be an object";
			return false;
		}
		const Dictionary dict = p_json;
		for (const KeyValue<Variant, Variant> &entry : dict) {
			Vector<uint8_t> entry_bytes;
			ProtoWriter key_writer(entry_bytes);
			key_writer.string_field(1, (String)entry.key);
			Vector<uint8_t> value_bytes;
			if (!encode_value_well_known(p_registry, entry.value, value_bytes, r_error)) {
				return false;
			}
			ProtoWriter value_writer(entry_bytes);
			value_writer.message_field(2, value_bytes);
			ProtoWriter writer(r_out);
			writer.message_field(1, entry_bytes);
		}
		return true;
	}
	if (p_type_name == WKT_LIST_VALUE) {
		if (p_json.get_type() != Variant::ARRAY) {
			r_error = "google.protobuf.ListValue must be an array";
			return false;
		}
		const Array array = p_json;
		for (int i = 0; i < array.size(); i++) {
			Vector<uint8_t> value_bytes;
			if (!encode_value_well_known(p_registry, array[i], value_bytes, r_error)) {
				return false;
			}
			ProtoWriter writer(r_out);
			writer.message_field(1, value_bytes);
		}
		return true;
	}
	return false;
}

bool ProtoJson::encode_message(const GrpcRegistry &p_registry, const GrpcDescriptor &p_descriptor, const Variant &p_json,
		Vector<uint8_t> &r_out, String &r_error) {
	if (is_well_known(p_descriptor.full_name)) {
		return encode_well_known(p_registry, p_descriptor.full_name, p_json, r_out, r_error);
	}
	if (p_json.get_type() == Variant::NIL) {
		return true;
	}
	if (p_json.get_type() != Variant::DICTIONARY) {
		r_error = "message " + p_descriptor.full_name + " must be an object";
		return false;
	}

	const Dictionary dict = p_json;
	for (const KeyValue<Variant, Variant> &entry : dict) {
		String key = entry.key;
		const GrpcField *field = p_descriptor.field_by_json_name(key);
		if (!field) {
			// Unknown fields are ignored, matching DiscardUnknown.
			continue;
		}
		if (!encode_field(p_registry, *field, entry.value, r_out, r_error)) {
			return false;
		}
	}
	return true;
}

// ---------------------------------------------------------------- decoding

static Variant decode_value_well_known(const uint8_t *p_data, int p_length);

static Variant decode_struct(const uint8_t *p_data, int p_length) {
	Dictionary out;
	ProtoReader reader(p_data, p_length);
	while (!reader.eof()) {
		int field = 0;
		int wire = 0;
		if (!reader.read_tag(field, wire)) {
			break;
		}
		if (field == 1 && wire == WIRE_LENGTH) {
			const uint8_t *entry_data = nullptr;
			int entry_length = 0;
			if (!reader.read_bytes(entry_data, entry_length)) {
				break;
			}
			String key;
			Variant value;
			ProtoReader entry_reader(entry_data, entry_length);
			while (!entry_reader.eof()) {
				int entry_field = 0;
				int entry_wire = 0;
				if (!entry_reader.read_tag(entry_field, entry_wire)) {
					break;
				}
				if (entry_field == 1 && entry_wire == WIRE_LENGTH) {
					const uint8_t *data = nullptr;
					int length = 0;
					if (!entry_reader.read_bytes(data, length)) {
						break;
					}
					key = String::utf8((const char *)data, length);
				} else if (entry_field == 2 && entry_wire == WIRE_LENGTH) {
					const uint8_t *data = nullptr;
					int length = 0;
					if (!entry_reader.read_bytes(data, length)) {
						break;
					}
					value = decode_value_well_known(data, length);
				} else if (!entry_reader.skip(entry_wire)) {
					break;
				}
			}
			out[key] = value;
		} else if (!reader.skip(wire)) {
			break;
		}
	}
	return out;
}

static Variant decode_list_value(const uint8_t *p_data, int p_length) {
	Array out;
	ProtoReader reader(p_data, p_length);
	while (!reader.eof()) {
		int field = 0;
		int wire = 0;
		if (!reader.read_tag(field, wire)) {
			break;
		}
		if (field == 1 && wire == WIRE_LENGTH) {
			const uint8_t *data = nullptr;
			int length = 0;
			if (!reader.read_bytes(data, length)) {
				break;
			}
			out.push_back(decode_value_well_known(data, length));
		} else if (!reader.skip(wire)) {
			break;
		}
	}
	return out;
}

static Variant decode_value_well_known(const uint8_t *p_data, int p_length) {
	ProtoReader reader(p_data, p_length);
	while (!reader.eof()) {
		int field = 0;
		int wire = 0;
		if (!reader.read_tag(field, wire)) {
			break;
		}
		if (field == 1 && wire == WIRE_VARINT) {
			uint64_t value = 0;
			reader.read_varint(value);
			return Variant();
		}
		if (field == 2 && wire == WIRE_FIXED64) {
			uint64_t bits = 0;
			reader.read_fixed64(bits);
			return bits_double(bits);
		}
		if (field == 3 && wire == WIRE_LENGTH) {
			const uint8_t *data = nullptr;
			int length = 0;
			reader.read_bytes(data, length);
			return String::utf8((const char *)data, length);
		}
		if (field == 4 && wire == WIRE_VARINT) {
			uint64_t value = 0;
			reader.read_varint(value);
			return value != 0;
		}
		if (field == 5 && wire == WIRE_LENGTH) {
			const uint8_t *data = nullptr;
			int length = 0;
			reader.read_bytes(data, length);
			return decode_struct(data, length);
		}
		if (field == 6 && wire == WIRE_LENGTH) {
			const uint8_t *data = nullptr;
			int length = 0;
			reader.read_bytes(data, length);
			return decode_list_value(data, length);
		}
		if (!reader.skip(wire)) {
			break;
		}
	}
	return Variant();
}

static Variant decode_scalar(const GrpcField &p_field, int p_wire, ProtoReader &p_reader, bool &r_ok) {
	r_ok = true;
	uint64_t varint = 0;
	uint32_t fixed32 = 0;
	uint64_t fixed64 = 0;

	switch (p_field.type) {
		case TYPE_STRING: {
			const uint8_t *data = nullptr;
			int length = 0;
			if (!p_reader.read_bytes(data, length)) {
				r_ok = false;
				return Variant();
			}
			return String::utf8((const char *)data, length);
		}
		case TYPE_BYTES: {
			const uint8_t *data = nullptr;
			int length = 0;
			if (!p_reader.read_bytes(data, length)) {
				r_ok = false;
				return Variant();
			}
			return base64_encode(data, length);
		}
		case TYPE_BOOL:
			if (!p_reader.read_varint(varint)) {
				r_ok = false;
				return Variant();
			}
			return varint != 0;
		case TYPE_INT32:
			if (!p_reader.read_varint(varint)) {
				r_ok = false;
				return Variant();
			}
			return Variant((int64_t)(int32_t)varint);
		case TYPE_UINT32:
			if (!p_reader.read_varint(varint)) {
				r_ok = false;
				return Variant();
			}
			return Variant((int64_t)(uint32_t)varint);
		case TYPE_INT64:
			if (!p_reader.read_varint(varint)) {
				r_ok = false;
				return Variant();
			}
			return Variant(String::num_int64((int64_t)varint));
		case TYPE_UINT64:
			if (!p_reader.read_varint(varint)) {
				r_ok = false;
				return Variant();
			}
			return Variant(String::num_uint64(varint));
		case TYPE_SINT32:
			if (!p_reader.read_varint(varint)) {
				r_ok = false;
				return Variant();
			}
			return Variant((int64_t)(int32_t)zigzag_decode(varint));
		case TYPE_SINT64:
			if (!p_reader.read_varint(varint)) {
				r_ok = false;
				return Variant();
			}
			return Variant(String::num_int64(zigzag_decode(varint)));
		case TYPE_FIXED32:
			if (!p_reader.read_fixed32(fixed32)) {
				r_ok = false;
				return Variant();
			}
			return Variant((int64_t)fixed32);
		case TYPE_SFIXED32:
			if (!p_reader.read_fixed32(fixed32)) {
				r_ok = false;
				return Variant();
			}
			return Variant((int64_t)(int32_t)fixed32);
		case TYPE_FLOAT:
			if (!p_reader.read_fixed32(fixed32)) {
				r_ok = false;
				return Variant();
			}
			return bits_float(fixed32);
		case TYPE_FIXED64:
			if (!p_reader.read_fixed64(fixed64)) {
				r_ok = false;
				return Variant();
			}
			return Variant(String::num_uint64(fixed64));
		case TYPE_SFIXED64:
			if (!p_reader.read_fixed64(fixed64)) {
				r_ok = false;
				return Variant();
			}
			return Variant(String::num_int64((int64_t)fixed64));
		case TYPE_DOUBLE:
			if (!p_reader.read_fixed64(fixed64)) {
				r_ok = false;
				return Variant();
			}
			return bits_double(fixed64);
		case TYPE_ENUM:
			if (!p_reader.read_varint(varint)) {
				r_ok = false;
				return Variant();
			}
			if (p_field.enum_type) {
				String name = p_field.enum_type->find_name((int32_t)varint);
				if (!name.is_empty()) {
					return name;
				}
			}
			return Variant((int64_t)(int32_t)varint);
		default:
			r_ok = false;
			return Variant();
	}
}

static bool decode_message_value(const GrpcRegistry &p_registry, const GrpcField &p_field, const uint8_t *p_data,
		int p_length, Variant &r_out, String &r_error) {
	if (is_well_known(p_field.type_name)) {
		if (p_field.type_name == WKT_VALUE) {
			r_out = decode_value_well_known(p_data, p_length);
		} else if (p_field.type_name == WKT_STRUCT) {
			r_out = decode_struct(p_data, p_length);
		} else if (p_field.type_name == WKT_LIST_VALUE) {
			r_out = decode_list_value(p_data, p_length);
		} else {
			r_out = Variant(Dictionary());
		}
		return true;
	}
	if (!p_field.message_type) {
		r_error = "unknown message type " + p_field.type_name;
		return false;
	}
	return ProtoJson::decode_message(p_registry, *p_field.message_type, p_data, p_length, r_out, r_error);
}

static bool decode_field(const GrpcRegistry &p_registry, const GrpcDescriptor &p_message, const GrpcField &p_field,
		int p_wire, ProtoReader &p_reader, Dictionary &r_out, String &r_error) {
	if (p_field.is_map()) {
		if (p_wire != WIRE_LENGTH) {
			return p_reader.skip(p_wire);
		}
		const uint8_t *data = nullptr;
		int length = 0;
		if (!p_reader.read_bytes(data, length)) {
			return false;
		}
		const GrpcDescriptor *entry = p_field.map_entry;
		if (!entry) {
			return false;
		}
		const GrpcField *key_field = entry->field_by_number(1);
		const GrpcField *value_field = entry->field_by_number(2);
		if (!key_field || !value_field) {
			return false;
		}
		Variant key;
		Variant value;
		ProtoReader entry_reader(data, length);
		while (!entry_reader.eof()) {
			int entry_field = 0;
			int entry_wire = 0;
			if (!entry_reader.read_tag(entry_field, entry_wire)) {
				return false;
			}
			if (entry_field == key_field->number) {
				bool ok = false;
				key = decode_scalar(*key_field, entry_wire, entry_reader, ok);
				if (!ok) {
					return false;
				}
			} else if (entry_field == value_field->number) {
				if (value_field->is_message()) {
					const uint8_t *value_data = nullptr;
					int value_length = 0;
					if (!entry_reader.read_bytes(value_data, value_length)) {
						return false;
					}
					if (!decode_message_value(p_registry, *value_field, value_data, value_length, value, r_error)) {
						return false;
					}
				} else {
					bool ok = false;
					value = decode_scalar(*value_field, entry_wire, entry_reader, ok);
					if (!ok) {
						return false;
					}
				}
			} else if (!entry_reader.skip(entry_wire)) {
				return false;
			}
		}
		Dictionary map = r_out.has(p_field.name) ? (Dictionary)r_out[p_field.name] : Dictionary();
		String key_string;
		if (key_field->type == TYPE_STRING) {
			key_string = key;
		} else if (key_field->type == TYPE_BOOL) {
			key_string = (bool)key ? "true" : "false";
		} else {
			key_string = (String)key;
		}
		map[key_string] = value;
		r_out[p_field.name] = map;
		return true;
	}

	if (p_field.repeated()) {
		Array array = r_out.has(p_field.name) ? (Array)r_out[p_field.name] : Array();
		if (p_field.is_message()) {
			if (p_wire != WIRE_LENGTH) {
				return p_reader.skip(p_wire);
			}
			const uint8_t *data = nullptr;
			int length = 0;
			if (!p_reader.read_bytes(data, length)) {
				return false;
			}
			Variant value;
			if (!decode_message_value(p_registry, p_field, data, length, value, r_error)) {
				return false;
			}
			array.push_back(value);
		} else if (p_wire == WIRE_LENGTH && packable(p_field.type)) {
			const uint8_t *data = nullptr;
			int length = 0;
			if (!p_reader.read_bytes(data, length)) {
				return false;
			}
			ProtoReader packed_reader(data, length);
			while (!packed_reader.eof()) {
				bool ok = false;
				Variant value = decode_scalar(p_field, 0, packed_reader, ok);
				if (!ok) {
					return false;
				}
				array.push_back(value);
			}
		} else {
			bool ok = false;
			Variant value = decode_scalar(p_field, p_wire, p_reader, ok);
			if (!ok) {
				return false;
			}
			array.push_back(value);
		}
		r_out[p_field.name] = array;
		return true;
	}

	if (p_field.is_message()) {
		if (p_wire != WIRE_LENGTH) {
			return p_reader.skip(p_wire);
		}
		const uint8_t *data = nullptr;
		int length = 0;
		if (!p_reader.read_bytes(data, length)) {
			return false;
		}
		Variant value;
		if (!decode_message_value(p_registry, p_field, data, length, value, r_error)) {
			return false;
		}
		r_out[p_field.name] = value;
		return true;
	}

	bool ok = false;
	Variant value = decode_scalar(p_field, p_wire, p_reader, ok);
	if (!ok) {
		return false;
	}
	r_out[p_field.name] = value;
	return true;
}

bool ProtoJson::decode_message(const GrpcRegistry &p_registry, const GrpcDescriptor &p_descriptor, const uint8_t *p_data,
		int p_length, Variant &r_json, String &r_error) {
	if (p_descriptor.full_name == WKT_VALUE) {
		r_json = decode_value_well_known(p_data, p_length);
		return true;
	}
	if (p_descriptor.full_name == WKT_STRUCT) {
		r_json = decode_struct(p_data, p_length);
		return true;
	}
	if (p_descriptor.full_name == WKT_LIST_VALUE) {
		r_json = decode_list_value(p_data, p_length);
		return true;
	}
	if (p_descriptor.full_name == WKT_EMPTY) {
		r_json = Dictionary();
		return true;
	}

	Dictionary out;
	ProtoReader reader(p_data, p_length);
	while (!reader.eof()) {
		int field_number = 0;
		int wire = 0;
		if (!reader.read_tag(field_number, wire)) {
			r_error = "invalid protobuf payload for " + p_descriptor.full_name;
			return false;
		}
		const GrpcField *field = p_descriptor.field_by_number(field_number);
		if (!field) {
			if (!reader.skip(wire)) {
				r_error = "could not skip unknown field";
				return false;
			}
			continue;
		}
		if (!decode_field(p_registry, p_descriptor, *field, wire, reader, out, r_error)) {
			return false;
		}
	}
	r_json = out;
	return true;
}

} // namespace grpc
