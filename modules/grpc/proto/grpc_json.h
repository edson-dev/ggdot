/**************************************************************************/
/*  grpc_json.h                                                           */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#pragma once

#include "grpc_descriptor.h"

#include "core/variant/variant.h"

namespace grpc {

// Protobuf <-> JSON conversion driven by reflection descriptors.
class ProtoJson {
public:
	static bool encode_message(const GrpcRegistry &p_registry, const GrpcDescriptor &p_descriptor, const Variant &p_json,
			Vector<uint8_t> &r_out, String &r_error);
	static bool decode_message(const GrpcRegistry &p_registry, const GrpcDescriptor &p_descriptor, const uint8_t *p_data,
			int p_length, Variant &r_json, String &r_error);
};

} // namespace grpc
