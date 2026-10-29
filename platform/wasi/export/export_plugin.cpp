/**************************************************************************/
/*  export_plugin.cpp                                                     */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#include "export_plugin.h"

void EditorExportPlatformWASI::get_export_options(List<ExportOption> *r_options) const {
	EditorExportPlatformPC::get_export_options(r_options);

	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "wasi/dedicated_server"), true));
	r_options->push_back(ExportOption(PropertyInfo(Variant::INT, "wasi/memory_pages", PROPERTY_HINT_RANGE, "1024,4096,256"), 8192));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "wasi/initial_args"), ""));
}

String EditorExportPlatformWASI::get_template_file_name(const String &p_target, const String &p_arch) const {
	return "wasi_" + p_target + ".wasm";
}

List<String> EditorExportPlatformWASI::get_binary_extensions(const Ref<EditorExportPreset> &p_preset) const {
	List<String> extensions;
	extensions.push_back("wasm");
	return extensions;
}

void EditorExportPlatformWASI::get_preset_features(const Ref<EditorExportPreset> &p_preset, List<String> *r_features) const {
	EditorExportPlatformPC::get_preset_features(p_preset, r_features);
	r_features->push_back("wasi");
	if (bool(p_preset->get("wasi/dedicated_server"))) {
		r_features->push_back("dedicated_server");
	}
}

void EditorExportPlatformWASI::get_platform_features(List<String> *r_features) const {
	r_features->push_back("wasi");
	EditorExportPlatformPC::get_platform_features(r_features);
}

Error EditorExportPlatformWASI::export_project(const Ref<EditorExportPreset> &p_preset, bool p_debug, const String &p_path,
		BitField<EditorExportPlatform::DebugFlags> p_flags, bool p_notify) {
	// A .wasm file cannot carry an appended PCK: the module would no longer be
	// a valid WebAssembly binary. Keep the PCK next to the module and pass it
	// with --main-pack in the gok manifest.
	if (bool(p_preset->get("binary_format/embed_pck"))) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("WASI Export"),
				TTR("Embedding the PCK in a WASM module is not supported. Disable 'binary_format/embed_pck' and ship server.pck next to the module."));
		return ERR_INVALID_PARAMETER;
	}
	return EditorExportPlatformPC::export_project(p_preset, p_debug, p_path, p_flags, p_notify);
}
