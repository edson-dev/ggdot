/**************************************************************************/
/*  ip_wasi.h                                                             */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#pragma once

#include "core/io/ip.h"

// WASI has no DNS resolver. Numeric addresses are parsed locally; hostnames
// cannot be resolved (the worker has no outbound network anyway).
class IPWASI : public IP {
	GDSOFTCLASS(IPWASI, IP);

	void _resolve_hostname(List<IPAddress> &r_addresses, const String &p_hostname, Type p_type = TYPE_ANY) const override;

	static IP *_create_func() {
		return memnew(IPWASI);
	}

public:
	void get_local_interfaces(HashMap<String, Interface_Info> *r_interfaces) const override;

	static void make_default() {
		_create = _create_func;
	}

	IPWASI() = default;
};
