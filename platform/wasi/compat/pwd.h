/**************************************************************************/
/*  pwd.h (WASI compatibility shim)                                       */
/**************************************************************************/

#pragma once

#include <sys/types.h>

struct passwd {
	const char *pw_name;
	const char *pw_passwd;
	uid_t pw_uid;
	gid_t pw_gid;
	const char *pw_gecos;
	const char *pw_dir;
	const char *pw_shell;
};

static inline struct passwd *getpwuid(uid_t uid) {
	return nullptr;
}
static inline uid_t getuid(void) {
	return 0;
}
static inline uid_t geteuid(void) {
	return 0;
}
