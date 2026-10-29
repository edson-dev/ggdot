#include "api.h"

// Every platform's api/*.cpp is compiled into editor builds, so this file must
// be portable. The host bridge itself only exists in the WASI template.
#ifdef WASI_ENABLED

#include "platform/wasi/wasi_host.h"

#include "core/object/class_db.h"

void register_wasi_api() {
	ClassDB::register_class<WASIHost>();
}

void unregister_wasi_api() {
}

#else

void register_wasi_api() {
}

void unregister_wasi_api() {
}

#endif
