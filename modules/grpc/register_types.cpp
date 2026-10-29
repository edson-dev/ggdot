/**************************************************************************/
/*  register_types.cpp                                                    */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#include "register_types.h"

#include "grpc_call.h"
#include "grpc_client.h"
#include "grpc_request.h"
#include "grpc_stream.h"

#include "core/object/class_db.h"

void initialize_grpc_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	GDREGISTER_CLASS(GrpcClient);
	GDREGISTER_CLASS(GrpcCall);
	GDREGISTER_CLASS(GrpcRequest);
	GDREGISTER_CLASS(GrpcStream);
}

void uninitialize_grpc_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
}
