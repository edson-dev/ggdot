/**************************************************************************/
/*  grpc_transport.cpp                                                    */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#include "grpc_transport.h"

#include "core/crypto/crypto.h"
#include "core/os/os.h"

namespace grpc {

static uint64_t now_ms() {
	return OS::get_singleton()->get_ticks_msec();
}

Transport::~Transport() {
	close();
}

Error Transport::connect_to_host(const String &p_host, int p_port, bool p_use_tls, const String &p_server_name, uint64_t p_deadline_ms) {
	close();

	if (p_host.is_empty()) {
		return ERR_INVALID_PARAMETER;
	}

	IPAddress ip = IP::get_singleton()->resolve_hostname(p_host);
	if (ip.is_wildcard() && p_host != "0.0.0.0" && p_host != "::") {
		return ERR_CANT_RESOLVE;
	}

	tcp.instantiate();
	Error err = tcp->connect_to_host(ip, p_port);
	if (err != OK) {
		close();
		return err;
	}

	while (true) {
		tcp->poll();
		StreamPeerSocket::Status status = tcp->get_status();
		if (status == StreamPeerSocket::STATUS_CONNECTED) {
			break;
		}
		if (status == StreamPeerSocket::STATUS_ERROR) {
			close();
			return ERR_CANT_CONNECT;
		}
		if (now_ms() >= p_deadline_ms) {
			close();
			return ERR_TIMEOUT;
		}
		OS::get_singleton()->delay_usec(500);
	}
	tcp->set_no_delay(true);

	secure = p_use_tls;
	if (secure) {
		if (!StreamPeerTLS::is_available()) {
			close();
			return ERR_UNAVAILABLE;
		}
		tls = Ref<StreamPeerTLS>(StreamPeerTLS::create());
		if (tls.is_null()) {
			close();
			return ERR_UNAVAILABLE;
		}
		String server_name = p_server_name.is_empty() ? p_host : p_server_name;
		err = tls->connect_to_stream(tcp, server_name, TLSOptions::client());
		if (err != OK) {
			close();
			return err;
		}
		while (true) {
			tls->poll();
			StreamPeerTLS::Status status = tls->get_status();
			if (status == StreamPeerTLS::STATUS_CONNECTED) {
				break;
			}
			if (status != StreamPeerTLS::STATUS_HANDSHAKING && status != StreamPeerTLS::STATUS_DISCONNECTED) {
				close();
				return ERR_CANT_CONNECT;
			}
			if (now_ms() >= p_deadline_ms) {
				close();
				return ERR_TIMEOUT;
			}
			OS::get_singleton()->delay_usec(500);
		}
	}

	opened = true;
	return OK;
}

Error Transport::write_all(const uint8_t *p_data, int p_length, uint64_t p_deadline_ms) {
	if (!opened) {
		return ERR_UNCONFIGURED;
	}
	Ref<StreamPeer> conn = peer();
	int written = 0;
	while (written < p_length) {
		if (secure) {
			tls->poll();
			if (tls->get_status() != StreamPeerTLS::STATUS_CONNECTED) {
				close();
				return ERR_CONNECTION_ERROR;
			}
		}
		int sent = 0;
		Error err = conn->put_partial_data(p_data + written, p_length - written, sent);
		if (err != OK) {
			close();
			return err;
		}
		if (sent > 0) {
			written += sent;
			continue;
		}
		if (now_ms() >= p_deadline_ms) {
			close();
			return ERR_TIMEOUT;
		}
		OS::get_singleton()->delay_usec(500);
	}
	return OK;
}

int Transport::read_some(uint8_t *p_buffer, int p_length, uint64_t p_deadline_ms) {
	if (!opened) {
		return -1;
	}
	Ref<StreamPeer> conn = peer();

	while (true) {
		if (secure) {
			tls->poll();
			StreamPeerTLS::Status status = tls->get_status();
			if (status == StreamPeerTLS::STATUS_ERROR || status == StreamPeerTLS::STATUS_ERROR_HOSTNAME_MISMATCH) {
				close();
				return -1;
			}
		} else {
			tcp->poll();
			StreamPeerSocket::Status status = tcp->get_status();
			if (status == StreamPeerSocket::STATUS_ERROR) {
				close();
				return -1;
			}
		}

		int available = conn->get_available_bytes();
		if (available > 0) {
			int to_read = MIN(available, p_length);
			int received = 0;
			Error err = conn->get_partial_data(p_buffer, to_read, received);
			if (err != OK) {
				close();
				return -1;
			}
			if (received > 0) {
				return received;
			}
		}

		// Report end of stream once the peer is gone and the buffer is empty.
		if (!secure && tcp->get_status() != StreamPeerSocket::STATUS_CONNECTED && available == 0) {
			close();
			return -1;
		}

		if (now_ms() >= p_deadline_ms) {
			return 0;
		}
		OS::get_singleton()->delay_usec(500);
	}
}

void Transport::close() {
	if (tls.is_valid()) {
		tls->disconnect_from_stream();
	}
	if (tcp.is_valid()) {
		tcp->disconnect_from_host();
	}
	tls.unref();
	tcp.unref();
	opened = false;
	secure = false;
}

} // namespace grpc
