/**************************************************************************/
/*  utsname.h (WASI compatibility shim)                                   */
/**************************************************************************/

#pragma once

struct utsname {
	char sysname[65];
	char nodename[65];
	char release[65];
	char version[65];
	char machine[65];
	char domainname[65];
};

extern "C" {
int uname(struct utsname *buf);
}
