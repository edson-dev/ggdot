/**************************************************************************/
/*  xattr.h (WASI compatibility shim)                                     */
/**************************************************************************/

#pragma once

#include <sys/types.h>

// WASI has no extended attributes. file_access_unix.cpp only uses these for
// permission metadata; failing cleanly keeps the file APIs working.
static inline int getxattr(const char *path, const char *name, void *value, size_t size) {
	return -1;
}
static inline int listxattr(const char *path, char *list, size_t size) {
	return -1;
}
static inline int setxattr(const char *path, const char *name, const void *value, size_t size, int flags) {
	return -1;
}
static inline int removexattr(const char *path, const char *name) {
	return -1;
}
