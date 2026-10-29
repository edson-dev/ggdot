#pragma once

#include "core/object/object.h"
#include "core/variant/dictionary.h"

// WASIHost is the GDScript surface for the gok host bridge. The worker has no
// outbound network; gok exposes a narrow `gok.http_request` import that
// proxies the auth/character REST calls to the gok app.
class WASIHost : public Object {
	GDCLASS(WASIHost, Object);

protected:
	static void _bind_methods();

public:
	// Returns {"ok": bool, "status": int, "json": Variant, "body": String,
	// "error": String}. Only available in the WASI template.
	static Dictionary fetch_json(const String &p_method, const String &p_path, const Dictionary &p_headers = Dictionary(), const String &p_body = String());
};
