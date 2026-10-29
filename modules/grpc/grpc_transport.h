/**************************************************************************/
/*  grpc_transport.h                                                      */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#pragma once

#include "core/io/ip.h"
#include "core/io/stream_peer_tcp.h"
#include "core/io/stream_peer_tls.h"
#include "core/object/ref_counted.h"

namespace grpc {

// Blocking TCP/TLS transport for one HTTP/2 connection. Every instance is
// confined to the thread that runs the call.
class Transport {
	Ref<StreamPeerTCP> tcp;
	Ref<StreamPeerTLS> tls;
	bool secure = false;
	bool opened = false;

	Ref<StreamPeer> peer() const {
		return secure ? Ref<StreamPeer>(tls) : Ref<StreamPeer>(tcp);
	}

public:
	Transport() = default;
	~Transport();

	Error connect_to_host(const String &p_host, int p_port, bool p_use_tls, const String &p_server_name, uint64_t p_deadline_ms);
	Error write_all(const uint8_t *p_data, int p_length, uint64_t p_deadline_ms);
	// Returns > 0 on data, 0 when the deadline elapsed without data, -1 on error.
	int read_some(uint8_t *p_buffer, int p_length, uint64_t p_deadline_ms);
	void close();
	bool is_open() const { return opened; }
};

} // namespace grpc
