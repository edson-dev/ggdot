/**************************************************************************/
/*  stubs.cpp                                                             */
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

// Symbols referenced by drivers/unix/os_unix.cpp that wasi-libc does not
// provide. The engine never calls these on WASI (OS_WASI overrides the public
// APIs), but the translation unit needs definitions to link.

#include <cerrno>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>

extern "C" {

pid_t fork(void) {
	errno = ENOSYS;
	return -1;
}

pid_t waitpid(pid_t p_pid, int *r_status, int p_options) {
	errno = ENOSYS;
	return -1;
}

int execv(const char *p_path, char *const p_argv[]) {
	errno = ENOSYS;
	return -1;
}

int execvp(const char *p_file, char *const p_argv[]) {
	errno = ENOSYS;
	return -1;
}

int uname(struct utsname *p_buf) {
	if (p_buf) {
		p_buf->sysname[0] = '\0';
		p_buf->nodename[0] = '\0';
		p_buf->release[0] = '\0';
		p_buf->version[0] = '\0';
		p_buf->machine[0] = '\0';
		p_buf->domainname[0] = '\0';
	}
	return -1;
}

// libc++ for wasm32-wasip1 omits thread_local destructor registration unless
// the threads proposal is enabled; the engine never relies on it here.
int __cxa_thread_atexit(void (*p_dtor)(void *), void *p_obj, void *p_dso_handle) {
	return 0;
}

int __cxa_thread_atexit_impl(void (*p_dtor)(void *), void *p_obj, void *p_dso_handle) {
	return 0;
}

int getpid(void) {
	return 1;
}

} // extern "C"
