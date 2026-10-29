/**************************************************************************/
/*  ip_wasi.cpp                                                           */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#include "ip_wasi.h"

void IPWASI::_resolve_hostname(List<IPAddress> &r_addresses, const String &p_hostname, Type p_type) const {
	// Accept a literal IPv4/IPv6 address; there is no resolver on WASI.
	IPAddress address = IPAddress(p_hostname);
	if (!address.is_wildcard()) {
		r_addresses.push_back(address);
	}
}

void IPWASI::get_local_interfaces(HashMap<String, Interface_Info> *r_interfaces) const {
}
