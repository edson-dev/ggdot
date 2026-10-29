/**************************************************************************/
/*  export_plugin.h                                                       */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#pragma once

#include "editor/export/editor_export_platform_pc.h"

class EditorExportPlatformWASI : public EditorExportPlatformPC {
	GDCLASS(EditorExportPlatformWASI, EditorExportPlatformPC);

public:
	void get_export_options(List<ExportOption> *r_options) const override;
	String get_template_file_name(const String &p_target, const String &p_arch) const override;
	List<String> get_binary_extensions(const Ref<EditorExportPreset> &p_preset) const override;
	void get_preset_features(const Ref<EditorExportPreset> &p_preset, List<String> *r_features) const override;
	void get_platform_features(List<String> *r_features) const override;
	Error export_project(const Ref<EditorExportPreset> &p_preset, bool p_debug, const String &p_path,
			BitField<EditorExportPlatform::DebugFlags> p_flags = 0, bool p_notify = true) override;
};
