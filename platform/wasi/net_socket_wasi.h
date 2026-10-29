/**************************************************************************/
/*  net_socket_wasi.h                                                     */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#pragma once

#include "core/io/net_socket.h"

// WASI preview1 exposes no socket()/bind()/listen(). The gok worker pre-opens
// TCP listeners and hands the guest file descriptors (fd 3 for the first
// `ports` entry). This implementation only supports those inherited sockets:
// accept/read/write work, creating or connecting sockets does not.
class NetSocketWASI : public NetSocket {
	GDSOFTCLASS(NetSocketWASI, NetSocket);

	int fd = -1;
	bool listening = false;

protected:
	static Ref<NetSocket> _create_func();

public:
	static void make_default();
	// Wrap an inherited (pre-opened) listener descriptor.
	static Ref<NetSocket> adopt_listener(int p_fd);

	Error open(Family p_family, Type p_sock_type, IP::Type &ip_type) override;
	void close() override;
	Error bind(Address p_addr) override;
	Error listen(int p_max_pending) override;
	Error connect_to_host(Address p_addr) override;
	Error poll(PollType p_type, int timeout) const override;
	Error recv(uint8_t *p_buffer, int p_len, int &r_read) override;
	Error recvfrom(uint8_t *p_buffer, int p_len, int &r_read, IPAddress &r_ip, uint16_t &r_port, bool p_peek = false) override;
	Error send(const uint8_t *p_buffer, int p_len, int &r_sent) override;
	Error sendto(const uint8_t *p_buffer, int p_len, int &r_sent, IPAddress p_ip, uint16_t p_port) override;
	Ref<NetSocket> accept(Address &r_addr) override;

	bool is_open() const override { return fd >= 0; }
	int get_available_bytes() const override;
	Error get_socket_address(Address *r_addr) const override;

	Error set_broadcasting_enabled(bool p_enabled) override { return ERR_UNAVAILABLE; }
	void set_blocking_enabled(bool p_enabled) override;
	void set_ipv6_only_enabled(bool p_enabled) override {}
	void set_tcp_no_delay_enabled(bool p_enabled) override {}
	void set_reuse_address_enabled(bool p_enabled) override {}
	Error join_multicast_group(const IPAddress &p_multi_address, const String &p_if_name) override { return ERR_UNAVAILABLE; }
	Error leave_multicast_group(const IPAddress &p_multi_address, const String &p_if_name) override { return ERR_UNAVAILABLE; }

	~NetSocketWASI() override;
};
