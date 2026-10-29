/**************************************************************************/
/*  export.cpp                                                            */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#include "export.h"

#include "export_plugin.h"

#include "core/object/class_db.h"
#include "editor/export/editor_export.h"

void register_wasi_exporter_types() {
	GDREGISTER_VIRTUAL_CLASS(EditorExportPlatformWASI);
}

void register_wasi_exporter() {
	Ref<EditorExportPlatformWASI> platform;
	platform.instantiate();
	platform->set_name("WASI");
	platform->set_os_name("WASI");
	platform->set_chmod_flags(0644);
	EditorExport::get_singleton()->add_export_platform(platform);
}
