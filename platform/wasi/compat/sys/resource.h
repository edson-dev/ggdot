/**************************************************************************/
/*  resource.h (WASI compatibility shim)                                  */
/**************************************************************************/

#pragma once

#include <sys/types.h>

#define RLIMIT_STACK 3
#define RLIMIT_CORE 4
#define RLIMIT_NOFILE 7

struct rlimit {
	unsigned long rlim_cur;
	unsigned long rlim_max;
};

static inline int getrlimit(int resource, struct rlimit *rlim) {
	if (rlim) {
		rlim->rlim_cur = 0;
		rlim->rlim_max = 0;
	}
	return -1;
}
static inline int setrlimit(int resource, const struct rlimit *rlim) {
	return -1;
}
