/**************************************************************************/
/*  wait.h (WASI compatibility shim)                                      */
/**************************************************************************/

#pragma once

#include <sys/types.h>

#define WNOHANG 1
#define WEXITSTATUS(status) (((status) & 0xff00) >> 8)
#define WTERMSIG(status) ((status) & 0x7f)
#define WIFEXITED(status) (true)
#define WIFSIGNALED(status) (false)

extern "C" {
pid_t fork(void);
pid_t waitpid(pid_t pid, int *status, int options);
int execv(const char *path, char *const argv[]);
int execvp(const char *file, char *const argv[]);
}
