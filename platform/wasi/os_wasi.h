/**************************************************************************/
/*  os_wasi.h                                                             */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#pragma once

#include "drivers/unix/os_unix.h"

// WASI preview1 command-module OS. Everything is headless: there is no window,
// GPU context, audio device, subprocess or dynamic library support.
class OS_WASI : public OS_Unix {
	MainLoop *main_loop = nullptr;

protected:
	void initialize() override;
	void finalize() override;
	void initialize_joypads() override {}
	bool _check_internal_feature_support(const String &p_feature) override;

public:
	static OS_WASI *get_singleton();

	OS_WASI();

	void set_main_loop(MainLoop *p_main_loop) override { main_loop = p_main_loop; }
	void delete_main_loop() override;
	MainLoop *get_main_loop() const override { return main_loop; }

	void run();

	String get_name() const override;
	String get_distribution_name() const override;
	String get_version() const override;

	String get_executable_path() const override;
	String get_user_data_dir(const String &p_user_dir) const override;
	String get_cache_path() const override;
	String get_config_path() const override;
	String get_data_path() const override;
	String get_temp_path() const override;
	String get_locale() const override;

	int get_processor_count() const override;
	int get_default_thread_pool_size() const override;
	Dictionary get_memory_info() const override;

	uint64_t get_ticks_usec() const override;
	void delay_usec(uint32_t p_usec) const override;

	Error get_entropy(uint8_t *r_buffer, int p_bytes) override;
	Error open_dynamic_library(const String &p_path, void *&p_library_handle, GDExtensionData *p_data = nullptr) override;

	Error execute(const String &p_path, const List<String> &p_arguments, String *r_pipe = nullptr, int *r_exitcode = nullptr,
			bool read_stderr = false, Mutex *p_pipe_mutex = nullptr, bool p_open_console = false) override;
	Dictionary execute_with_pipe(const String &p_path, const List<String> &p_arguments, bool p_blocking = true) override;
	Error create_process(const String &p_path, const List<String> &p_arguments, ProcessID *r_child_id = nullptr,
			bool p_open_console = false) override;
	Error kill(const ProcessID &p_pid) override;
	bool is_process_running(const ProcessID &p_pid) const override;
	int get_process_exit_code(const ProcessID &p_pid) const override;

	Error shell_open(const String &p_uri) override;
	Error move_to_trash(const String &p_path) override;
};
