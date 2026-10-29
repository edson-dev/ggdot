/**************************************************************************/
/*  wasi_main.cpp                                                         */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#include "os_wasi.h"

#include "main/main.h"

#include <cstdlib>

// WASI command modules enter through `_start` (provided by wasi-libc's
// crt1-command), which calls main(). Keep the same setup/teardown as the other
// Unix platforms.
int main(int argc, char *argv[]) {
	OS_WASI os;

	Error err = Main::setup(argv[0], argc - 1, &argv[1]);
	if (err != OK) {
		if (err == ERR_HELP) {
			return EXIT_SUCCESS;
		}
		return EXIT_FAILURE;
	}

	if (Main::start() == EXIT_SUCCESS) {
		os.run();
	} else {
		os.set_exit_code(EXIT_FAILURE);
	}

	Main::cleanup();
	return os.get_exit_code();
}
