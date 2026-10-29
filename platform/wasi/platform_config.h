/**************************************************************************/
/*  platform_config.h                                                     */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#pragma once

#ifdef __wasi__
#define WASI_ENABLED 1
#endif

#if defined(__wasm__) && !defined(__wasi__)
#error "platform=wasi requires the wasm32-wasip1 target."
#endif

// WASI preview1 has no threads. Godot's no-thread code paths are selected by
// the absence of THREADS_ENABLED.

#include <alloca.h>
