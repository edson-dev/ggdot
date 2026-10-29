/**************************************************************************/
/*  os_wasi.cpp                                                           */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#include "os_wasi.h"

#include "ip_wasi.h"
#include "net_socket_wasi.h"

#include "core/os/main_loop.h"
#include "core/version.h"
#include "main/main.h"
#include "servers/display/display_server.h"

#include <wasi/api.h>

#include <cerrno>
#include <ctime>
#include <unistd.h>

OS_WASI *OS_WASI::get_singleton() {
	return static_cast<OS_WASI *>(OS::get_singleton());
}

OS_WASI::OS_WASI() = default;

void OS_WASI::initialize() {
	OS_Unix::initialize_core();
	IPWASI::make_default();
	NetSocketWASI::make_default();

	// Keep the process descriptor-backed stdio; wasi-libc maps it to fd 1/2,
	// which is what the gok worker captures as the log tail.
	set_environment("GODOT_WASI", "1");
}

void OS_WASI::delete_main_loop() {
	if (main_loop) {
		memdelete(main_loop);
		main_loop = nullptr;
	}
}

void OS_WASI::finalize() {
	OS_Unix::finalize_core();
}

void OS_WASI::run() {
	if (!main_loop) {
		return;
	}
	main_loop->initialize();

	while (true) {
		if (DisplayServer::get_singleton()) {
			DisplayServer::get_singleton()->process_events();
		}
		if (Main::iteration()) {
			break;
		}
	}
}

bool OS_WASI::_check_internal_feature_support(const String &p_feature) {
	return p_feature == "wasi" || p_feature == "dedicated_server" || p_feature == "headless";
}

String OS_WASI::get_name() const {
	return "WASI";
}

String OS_WASI::get_distribution_name() const {
	return "WASI";
}

String OS_WASI::get_version() const {
	return String(GODOT_VERSION_FULL_CONFIG) + " wasm32-wasip1";
}

String OS_WASI::get_executable_path() const {
	// A WASI command module has no filesystem path of its own. The gok worker
	// runs the module from the preopened module directory.
	return "/godot-server.wasm";
}

String OS_WASI::get_user_data_dir(const String &p_user_dir) const {
	// There is no writable user:// storage in the worker: the preopen is
	// read-only. Return a path under the read-only root so accidental writes
	// fail loudly instead of silently persisting.
	return String("/user").path_join(p_user_dir);
}

String OS_WASI::get_cache_path() const {
	return "/tmp";
}

String OS_WASI::get_config_path() const {
	return "/";
}

String OS_WASI::get_data_path() const {
	return "/";
}

String OS_WASI::get_temp_path() const {
	return "/tmp";
}

String OS_WASI::get_locale() const {
	return "C";
}

int OS_WASI::get_processor_count() const {
	return 1;
}

int OS_WASI::get_default_thread_pool_size() const {
	return 1;
}

Dictionary OS_WASI::get_memory_info() const {
	Dictionary meminfo;
	size_t pages = __builtin_wasm_memory_size(0);
	int64_t current = (int64_t)pages * 65536;
	meminfo["physical"] = current;
	meminfo["free"] = 0;
	meminfo["available"] = 0;
	meminfo["stack"] = 8 * 1024 * 1024;
	return meminfo;
}

uint64_t OS_WASI::get_ticks_usec() const {
	struct timespec tv_now = { 0, 0 };
	clock_gettime(CLOCK_MONOTONIC, &tv_now);
	return ((uint64_t)tv_now.tv_nsec / 1000L) + (uint64_t)tv_now.tv_sec * 1000000L;
}

void OS_WASI::delay_usec(uint32_t p_usec) const {
	struct timespec requested = { static_cast<time_t>(p_usec / 1000000), (static_cast<long>(p_usec) % 1000000) * 1000 };
	struct timespec remaining;
	while (nanosleep(&requested, &remaining) == -1 && errno == EINTR) {
		requested.tv_sec = remaining.tv_sec;
		requested.tv_nsec = remaining.tv_nsec;
	}
}

Error OS_WASI::get_entropy(uint8_t *r_buffer, int p_bytes) {
	__wasi_errno_t err = __wasi_random_get(r_buffer, (size_t)p_bytes);
	return err == __WASI_ERRNO_SUCCESS ? OK : FAILED;
}

Error OS_WASI::open_dynamic_library(const String &p_path, void *&p_library_handle, GDExtensionData *p_data) {
	ERR_FAIL_V_MSG(ERR_UNAVAILABLE, "Dynamic libraries are not available on WASI.");
}

Error OS_WASI::execute(const String &p_path, const List<String> &p_arguments, String *r_pipe, int *r_exitcode, bool read_stderr,
		Mutex *p_pipe_mutex, bool p_open_console) {
	ERR_FAIL_V_MSG(ERR_UNAVAILABLE, "OS::execute is not available on WASI.");
}

Dictionary OS_WASI::execute_with_pipe(const String &p_path, const List<String> &p_arguments, bool p_blocking) {
	ERR_FAIL_V_MSG(Dictionary(), "OS::execute_with_pipe is not available on WASI.");
}

Error OS_WASI::create_process(const String &p_path, const List<String> &p_arguments, ProcessID *r_child_id, bool p_open_console) {
	ERR_FAIL_V_MSG(ERR_UNAVAILABLE, "OS::create_process is not available on WASI.");
}

Error OS_WASI::kill(const ProcessID &p_pid) {
	ERR_FAIL_V_MSG(ERR_UNAVAILABLE, "OS::kill is not available on WASI.");
}

bool OS_WASI::is_process_running(const ProcessID &p_pid) const {
	return false;
}

int OS_WASI::get_process_exit_code(const ProcessID &p_pid) const {
	return -1;
}

Error OS_WASI::shell_open(const String &p_uri) {
	return ERR_UNAVAILABLE;
}

Error OS_WASI::move_to_trash(const String &p_path) {
	return ERR_UNAVAILABLE;
}
