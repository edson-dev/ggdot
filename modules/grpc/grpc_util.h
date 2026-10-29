/**************************************************************************/
/*  grpc_util.h                                                           */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#pragma once

#include "grpc_types.h"

#include "core/variant/variant.h"

namespace grpc {

// Metadata JSON is an object of string values or arrays of strings, matching
// the Go extension. Keys are lower-cased and binary keys are base64 encoded.
Vector<Header> metadata_from_json(const String &p_json, String &r_error);
Vector<Header> metadata_from_dict(const Dictionary &p_metadata, String &r_error);

Dictionary status_to_dict(const CallResult &p_result);
String status_to_json(const CallResult &p_result);

// Concatenates messages as 4-byte big-endian length frames (Go addon parity).
Vector<uint8_t> frame_messages(const Vector<Vector<uint8_t>> &p_messages);

} // namespace grpc
