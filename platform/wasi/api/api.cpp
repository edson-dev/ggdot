#include "api.h"

#include "platform/wasi/wasi_host.h"

#include "core/object/class_db.h"

void register_wasi_api() {
	ClassDB::register_class<WASIHost>();
}

void unregister_wasi_api() {
}
