/**************************************************************************/
/*  grpc_descriptor.h                                                     */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#pragma once

#include "grpc_binary.h"

#include "core/string/ustring.h"
#include "core/templates/hash_map.h"
#include "core/templates/vector.h"

namespace grpc {

struct GrpcDescriptor;
struct GrpcEnum;

struct GrpcEnumValue {
	String name;
	int number = 0;
};

struct GrpcEnum {
	String full_name;
	Vector<GrpcEnumValue> values;

	int find_number(const String &p_name) const {
		for (const GrpcEnumValue &v : values) {
			if (v.name == p_name) {
				return v.number;
			}
		}
		return 0;
	}

	String find_name(int p_number) const {
		for (const GrpcEnumValue &v : values) {
			if (v.number == p_number) {
				return v.name;
			}
		}
		return String();
	}
};

struct GrpcField {
	String name;
	String json_name;
	String type_name;
	int number = 0;
	int label = LABEL_OPTIONAL;
	int type = TYPE_STRING;
	int oneof_index = -1;
	bool proto3_optional = false;

	const GrpcDescriptor *message_type = nullptr;
	const GrpcEnum *enum_type = nullptr;
	// Set when message_type is a synthesized map entry message.
	const GrpcDescriptor *map_entry = nullptr;

	bool repeated() const { return label == LABEL_REPEATED; }
	bool is_map() const { return map_entry != nullptr; }
	bool is_message() const { return type == TYPE_MESSAGE || type == TYPE_GROUP; }
};

struct GrpcDescriptor {
	String full_name;
	Vector<GrpcField> fields;
	bool map_entry = false;
	bool proto3 = true;

	const GrpcField *field_by_number(int p_number) const {
		for (const GrpcField &f : fields) {
			if (f.number == p_number) {
				return &f;
			}
		}
		return nullptr;
	}

	const GrpcField *field_by_json_name(const String &p_name) const {
		for (const GrpcField &f : fields) {
			if (f.json_name == p_name || f.name == p_name) {
				return &f;
			}
		}
		return nullptr;
	}
};

struct GrpcMethod {
	String name;
	String input_type;
	String output_type;
	bool client_streaming = false;
	bool server_streaming = false;
};

struct GrpcService {
	String full_name;
	Vector<GrpcMethod> methods;

	const GrpcMethod *find_method(const String &p_name) const {
		for (const GrpcMethod &m : methods) {
			if (m.name == p_name) {
				return &m;
			}
		}
		return nullptr;
	}
};

class GrpcRegistry {
	HashMap<String, GrpcDescriptor> messages;
	HashMap<String, GrpcEnum> enums;
	HashMap<String, GrpcService> services;
	Vector<String> pending_files;

	bool _parse_file_descriptor(const uint8_t *p_data, int p_length);
	bool _parse_message(const uint8_t *p_data, int p_length, const String &p_parent, bool p_proto3);
	bool _parse_enum(const uint8_t *p_data, int p_length, const String &p_parent);
	bool _parse_service(const uint8_t *p_data, int p_length, const String &p_package);
	void _parse_field(const uint8_t *p_data, int p_length, GrpcDescriptor &r_message);
	String _qualify(const String &p_parent, const String &p_name) const;

public:
	void clear();
	bool parse_file(const Vector<uint8_t> &p_file_descriptor_proto);
	void resolve();

	const GrpcDescriptor *find_message(const String &p_name) const {
		String name = p_name.begins_with(".") ? p_name.substr(1) : p_name;
		return messages.getptr(name);
	}
	const GrpcEnum *find_enum(const String &p_name) const {
		String name = p_name.begins_with(".") ? p_name.substr(1) : p_name;
		return enums.getptr(name);
	}
	const GrpcService *find_service(const String &p_name) const {
		String name = p_name.begins_with(".") ? p_name.substr(1) : p_name;
		return services.getptr(name);
	}
};

} // namespace grpc
