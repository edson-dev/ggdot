/**************************************************************************/
/*  dlfcn.h (WASI compatibility shim)                                     */
/**************************************************************************/

#pragma once

// Dynamic loading does not exist on WASI. OS_WASI overrides the engine entry
// points; these inline stubs keep drivers/unix/os_unix.cpp linkable (the iconv
// fallback simply reports unavailable).
#define RTLD_LAZY 1
#define RTLD_NOW 2
#define RTLD_GLOBAL 4
#define RTLD_LOCAL 8
#define RTLD_DEEPBIND 16
#define RTLD_NEXT ((void *)-1)

static inline void *dlopen(const char *filename, int flags) {
	return nullptr;
}
static inline void *dlsym(void *handle, const char *symbol) {
	return nullptr;
}
static inline int dlclose(void *handle) {
	return -1;
}
static inline char *dlerror() {
	return (char *)"dynamic loading is not available on WASI";
}
