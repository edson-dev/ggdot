/**************************************************************************/
/*  grpc_descriptor.cpp                                                   */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#include "grpc_descriptor.h"

#include "core/string/char_utils.h"

namespace grpc {

static String default_json_name(const String &p_name) {
	String out;
	bool upper = false;
	for (int i = 0; i < p_name.length(); i++) {
		char32_t c = p_name[i];
		if (c == '_') {
			upper = true;
			continue;
		}
		if (upper) {
			char32_t up = c;
			if (up >= 'a' && up <= 'z') {
				up = up - 'a' + 'A';
			}
			out += String::chr(up);
			upper = false;
		} else {
			out += c;
		}
	}
	return out;
}

void GrpcRegistry::clear() {
	messages.clear();
	enums.clear();
	services.clear();
	pending_files.clear();
}

String GrpcRegistry::_qualify(const String &p_parent, const String &p_name) const {
	if (p_parent.is_empty()) {
		return p_name;
	}
	return p_parent + "." + p_name;
}

bool GrpcRegistry::parse_file(const Vector<uint8_t> &p_file_descriptor_proto) {
	return _parse_file_descriptor(p_file_descriptor_proto.ptr(), p_file_descriptor_proto.size());
}

bool GrpcRegistry::_parse_file_descriptor(const uint8_t *p_data, int p_length) {
	ProtoReader reader(p_data, p_length);
	String package;
	bool proto3 = true;

	while (!reader.eof()) {
		int field = 0;
		int wire = 0;
		if (!reader.read_tag(field, wire)) {
			return false;
		}
		if (field == 2 && wire == WIRE_LENGTH) {
			const uint8_t *data = nullptr;
			int length = 0;
			if (!reader.read_bytes(data, length)) {
				return false;
			}
			package = String::utf8((const char *)data, length);
		} else if (field == 4 && wire == WIRE_LENGTH) {
			const uint8_t *data = nullptr;
			int length = 0;
			if (!reader.read_bytes(data, length)) {
				return false;
			}
			if (!_parse_message(data, length, package, proto3)) {
				return false;
			}
		} else if (field == 5 && wire == WIRE_LENGTH) {
			const uint8_t *data = nullptr;
			int length = 0;
			if (!reader.read_bytes(data, length)) {
				return false;
			}
			if (!_parse_enum(data, length, package)) {
				return false;
			}
		} else if (field == 6 && wire == WIRE_LENGTH) {
			const uint8_t *data = nullptr;
			int length = 0;
			if (!reader.read_bytes(data, length)) {
				return false;
			}
			if (!_parse_service(data, length, package)) {
				return false;
			}
		} else if (field == 12 && wire == WIRE_LENGTH) {
			const uint8_t *data = nullptr;
			int length = 0;
			if (!reader.read_bytes(data, length)) {
				return false;
			}
			proto3 = String::utf8((const char *)data, length) == "proto3";
		} else if (!reader.skip(wire)) {
			return false;
		}
	}
	return true;
}

bool GrpcRegistry::_parse_message(const uint8_t *p_data, int p_length, const String &p_parent, bool p_proto3) {
	String name;
	bool map_entry = false;

	{
		ProtoReader reader(p_data, p_length);
		while (!reader.eof()) {
			int field = 0;
			int wire = 0;
			if (!reader.read_tag(field, wire)) {
				return false;
			}
			if (field == 1 && wire == WIRE_LENGTH) {
				const uint8_t *data = nullptr;
				int length = 0;
				if (!reader.read_bytes(data, length)) {
					return false;
				}
				name = String::utf8((const char *)data, length);
			} else if (field == 7 && wire == WIRE_LENGTH) {
				const uint8_t *data = nullptr;
				int length = 0;
				if (!reader.read_bytes(data, length)) {
					return false;
				}
				ProtoReader options(data, length);
				while (!options.eof()) {
					int option_field = 0;
					int option_wire = 0;
					if (!options.read_tag(option_field, option_wire)) {
						return false;
					}
					if (option_field == 7 && option_wire == WIRE_VARINT) {
						uint64_t value = 0;
						if (!options.read_varint(value)) {
							return false;
						}
						map_entry = value != 0;
					} else if (!options.skip(option_wire)) {
						return false;
					}
				}
			} else if (!reader.skip(wire)) {
				return false;
			}
		}
	}

	if (name.is_empty()) {
		return false;
	}

	String full_name = _qualify(p_parent, name);

	// Nested types first, so the parent can be inserted once.
	{
		ProtoReader reader(p_data, p_length);
		while (!reader.eof()) {
			int field = 0;
			int wire = 0;
			if (!reader.read_tag(field, wire)) {
				return false;
			}
			if (field == 3 && wire == WIRE_LENGTH) {
				const uint8_t *data = nullptr;
				int length = 0;
				if (!reader.read_bytes(data, length)) {
					return false;
				}
				if (!_parse_message(data, length, full_name, p_proto3)) {
					return false;
				}
			} else if (field == 4 && wire == WIRE_LENGTH) {
				const uint8_t *data = nullptr;
				int length = 0;
				if (!reader.read_bytes(data, length)) {
					return false;
				}
				if (!_parse_enum(data, length, full_name)) {
					return false;
				}
			} else if (!reader.skip(wire)) {
				return false;
			}
		}
	}

	GrpcDescriptor descriptor;
	descriptor.full_name = full_name;
	descriptor.map_entry = map_entry;
	descriptor.proto3 = p_proto3;
	messages.insert(full_name, descriptor);

	GrpcDescriptor *stored = messages.getptr(full_name);
	if (!stored) {
		return false;
	}

	ProtoReader reader(p_data, p_length);
	while (!reader.eof()) {
		int field = 0;
		int wire = 0;
		if (!reader.read_tag(field, wire)) {
			return false;
		}
		if (field == 2 && wire == WIRE_LENGTH) {
			const uint8_t *data = nullptr;
			int length = 0;
			if (!reader.read_bytes(data, length)) {
				return false;
			}
			_parse_field(data, length, *stored);
		} else if (!reader.skip(wire)) {
			return false;
		}
	}

	return true;
}

void GrpcRegistry::_parse_field(const uint8_t *p_data, int p_length, GrpcDescriptor &r_message) {
	GrpcField field;
	ProtoReader reader(p_data, p_length);
	while (!reader.eof()) {
		int tag = 0;
		int wire = 0;
		if (!reader.read_tag(tag, wire)) {
			return;
		}
		if (tag == 1 && wire == WIRE_LENGTH) {
			const uint8_t *data = nullptr;
			int length = 0;
			if (!reader.read_bytes(data, length)) {
				return;
			}
			field.name = String::utf8((const char *)data, length);
		} else if (tag == 3 && wire == WIRE_VARINT) {
			uint64_t value = 0;
			if (!reader.read_varint(value)) {
				return;
			}
			field.number = (int)value;
		} else if (tag == 4 && wire == WIRE_VARINT) {
			uint64_t value = 0;
			if (!reader.read_varint(value)) {
				return;
			}
			field.label = (int)value;
		} else if (tag == 5 && wire == WIRE_VARINT) {
			uint64_t value = 0;
			if (!reader.read_varint(value)) {
				return;
			}
			field.type = (int)value;
		} else if (tag == 6 && wire == WIRE_LENGTH) {
			const uint8_t *data = nullptr;
			int length = 0;
			if (!reader.read_bytes(data, length)) {
				return;
			}
			field.type_name = String::utf8((const char *)data, length);
		} else if (tag == 9 && wire == WIRE_VARINT) {
			uint64_t value = 0;
			if (!reader.read_varint(value)) {
				return;
			}
			field.oneof_index = (int)value;
		} else if (tag == 10 && wire == WIRE_LENGTH) {
			const uint8_t *data = nullptr;
			int length = 0;
			if (!reader.read_bytes(data, length)) {
				return;
			}
			field.json_name = String::utf8((const char *)data, length);
		} else if (tag == 17 && wire == WIRE_VARINT) {
			uint64_t value = 0;
			if (!reader.read_varint(value)) {
				return;
			}
			field.proto3_optional = value != 0;
		} else if (!reader.skip(wire)) {
			return;
		}
	}
	if (field.name.is_empty()) {
		return;
	}
	if (field.json_name.is_empty()) {
		field.json_name = default_json_name(field.name);
	}
	r_message.fields.push_back(field);
}

bool GrpcRegistry::_parse_enum(const uint8_t *p_data, int p_length, const String &p_parent) {
	String name;
	Vector<GrpcEnumValue> values;

	ProtoReader reader(p_data, p_length);
	while (!reader.eof()) {
		int field = 0;
		int wire = 0;
		if (!reader.read_tag(field, wire)) {
			return false;
		}
		if (field == 1 && wire == WIRE_LENGTH) {
			const uint8_t *data = nullptr;
			int length = 0;
			if (!reader.read_bytes(data, length)) {
				return false;
			}
			name = String::utf8((const char *)data, length);
		} else if (field == 2 && wire == WIRE_LENGTH) {
			const uint8_t *data = nullptr;
			int length = 0;
			if (!reader.read_bytes(data, length)) {
				return false;
			}
			GrpcEnumValue value;
			ProtoReader value_reader(data, length);
			while (!value_reader.eof()) {
				int value_field = 0;
				int value_wire = 0;
				if (!value_reader.read_tag(value_field, value_wire)) {
					return false;
				}
				if (value_field == 1 && value_wire == WIRE_LENGTH) {
					const uint8_t *value_data = nullptr;
					int value_length = 0;
					if (!value_reader.read_bytes(value_data, value_length)) {
						return false;
					}
					value.name = String::utf8((const char *)value_data, value_length);
				} else if (value_field == 2 && value_wire == WIRE_VARINT) {
					uint64_t number = 0;
					if (!value_reader.read_varint(number)) {
						return false;
					}
					value.number = (int)(int32_t)number;
				} else if (!value_reader.skip(value_wire)) {
					return false;
				}
			}
			if (!value.name.is_empty()) {
				values.push_back(value);
			}
		} else if (!reader.skip(wire)) {
			return false;
		}
	}

	if (name.is_empty()) {
		return false;
	}
	GrpcEnum grpc_enum;
	grpc_enum.full_name = _qualify(p_parent, name);
	grpc_enum.values = values;
	enums.insert(grpc_enum.full_name, grpc_enum);
	return true;
}

bool GrpcRegistry::_parse_service(const uint8_t *p_data, int p_length, const String &p_package) {
	String name;
	Vector<GrpcMethod> methods;

	ProtoReader reader(p_data, p_length);
	while (!reader.eof()) {
		int field = 0;
		int wire = 0;
		if (!reader.read_tag(field, wire)) {
			return false;
		}
		if (field == 1 && wire == WIRE_LENGTH) {
			const uint8_t *data = nullptr;
			int length = 0;
			if (!reader.read_bytes(data, length)) {
				return false;
			}
			name = String::utf8((const char *)data, length);
		} else if (field == 2 && wire == WIRE_LENGTH) {
			const uint8_t *data = nullptr;
			int length = 0;
			if (!reader.read_bytes(data, length)) {
				return false;
			}
			GrpcMethod method;
			ProtoReader method_reader(data, length);
			while (!method_reader.eof()) {
				int method_field = 0;
				int method_wire = 0;
				if (!method_reader.read_tag(method_field, method_wire)) {
					return false;
				}
				if (method_field == 1 && method_wire == WIRE_LENGTH) {
					const uint8_t *value = nullptr;
					int value_length = 0;
					if (!method_reader.read_bytes(value, value_length)) {
						return false;
					}
					method.name = String::utf8((const char *)value, value_length);
				} else if (method_field == 2 && method_wire == WIRE_LENGTH) {
					const uint8_t *value = nullptr;
					int value_length = 0;
					if (!method_reader.read_bytes(value, value_length)) {
						return false;
					}
					method.input_type = String::utf8((const char *)value, value_length);
				} else if (method_field == 3 && method_wire == WIRE_LENGTH) {
					const uint8_t *value = nullptr;
					int value_length = 0;
					if (!method_reader.read_bytes(value, value_length)) {
						return false;
					}
					method.output_type = String::utf8((const char *)value, value_length);
				} else if (method_field == 5 && method_wire == WIRE_VARINT) {
					uint64_t value = 0;
					if (!method_reader.read_varint(value)) {
						return false;
					}
					method.client_streaming = value != 0;
				} else if (method_field == 6 && method_wire == WIRE_VARINT) {
					uint64_t value = 0;
					if (!method_reader.read_varint(value)) {
						return false;
					}
					method.server_streaming = value != 0;
				} else if (!method_reader.skip(method_wire)) {
					return false;
				}
			}
			if (!method.name.is_empty()) {
				methods.push_back(method);
			}
		} else if (!reader.skip(wire)) {
			return false;
		}
	}

	if (name.is_empty()) {
		return false;
	}
	GrpcService service;
	service.full_name = _qualify(p_package, name);
	service.methods = methods;
	services.insert(service.full_name, service);
	return true;
}

void GrpcRegistry::resolve() {
	for (KeyValue<String, GrpcDescriptor> &entry : messages) {
		for (GrpcField &field : entry.value.fields) {
			if (field.type == TYPE_MESSAGE || field.type == TYPE_GROUP) {
				const GrpcDescriptor *message = find_message(field.type_name);
				field.message_type = message;
				if (message && message->map_entry) {
					field.map_entry = message;
				}
			} else if (field.type == TYPE_ENUM) {
				field.enum_type = find_enum(field.type_name);
			}
		}
	}
}

} // namespace grpc
