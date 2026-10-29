/**************************************************************************/
/*  net_socket_wasi.cpp                                                   */
/*                                                                         */
/*  Freestanding implementation of NetSocket for inherited WASI fds.      */
/**************************************************************************/

#include "net_socket_wasi.h"

#include "core/os/os.h"

#include <wasi/api.h>
#include <poll.h>

#include <cerrno>
#include <unistd.h>

// wazero's WASI socket extension. Not part of the preview1 snapshot, so it is
// declared here instead of relying on wasi-libc headers.
extern "C" __wasi_errno_t sock_accept(__wasi_fd_t p_fd, __wasi_fdflags_t p_flags, __wasi_fd_t *r_result)
		__attribute__((__import_module__("wasi_snapshot_preview1"), __import_name__("sock_accept")));

Ref<NetSocket> NetSocketWASI::_create_func() {
	// Generic sockets are not the inherited listener. Creating one here must
	// NOT adopt the listener: StreamPeerTCP constructs a temporary NetSocket
	// for every accepted connection, and adopting/closing the listener there
	// would kill the server after the first client disconnects.
	return memnew(NetSocketWASI);
}

Ref<NetSocket> NetSocketWASI::adopt_preopened() {
	String configured = OS::get_singleton()->get_environment("GODOT_WASI_LISTENER_FD");
	int listener_fd = configured.is_valid_int() ? configured.to_int() : -1;
	if (listener_fd < 0) {
		// gok preopens the FS directory first and the TCP listener after it;
		// find the first stream socket instead of assuming fd 3.
		for (int fd = 3; fd < 64; fd++) {
			__wasi_fdstat_t st;
			if (__wasi_fd_fdstat_get((__wasi_fd_t)fd, &st) != __WASI_ERRNO_SUCCESS) {
				continue;
			}
			if (st.fs_filetype == __WASI_FILETYPE_SOCKET_STREAM) {
				listener_fd = fd;
				break;
			}
		}
	}
	if (listener_fd < 0) {
		return Ref<NetSocket>();
	}
	return adopt_listener(listener_fd);
}

void NetSocketWASI::make_default() {
	_create = _create_func;
}

Ref<NetSocket> NetSocketWASI::adopt_listener(int p_fd) {
	if (p_fd < 0) {
		return Ref<NetSocket>();
	}
	Ref<NetSocketWASI> socket = memnew(NetSocketWASI);
	socket->fd = p_fd;
	socket->listening = true;
	// Non-blocking accept is required: the worker must return to the WASM
	// runtime periodically so wazero can observe worker cancellation.
	__wasi_fd_fdstat_set_flags((__wasi_fd_t)p_fd, __WASI_FDFLAGS_NONBLOCK);
	return Ref<NetSocket>(socket);
}

NetSocketWASI::~NetSocketWASI() {
	close();
}

Error NetSocketWASI::open(Family p_family, Type p_sock_type, IP::Type &ip_type) {
	ERR_FAIL_V_MSG(ERR_UNAVAILABLE, "WASI cannot create sockets; only the pre-opened listener is available.");
}

void NetSocketWASI::close() {
	if (fd >= 0) {
		::close(fd);
		fd = -1;
		listening = false;
	}
}

Error NetSocketWASI::bind(Address p_addr) {
	return ERR_UNAVAILABLE;
}

Error NetSocketWASI::listen(int p_max_pending) {
	return ERR_UNAVAILABLE;
}

Error NetSocketWASI::connect_to_host(Address p_addr) {
	return ERR_UNAVAILABLE;
}

Error NetSocketWASI::poll(PollType p_type, int timeout) const {
	if (fd < 0) {
		return ERR_UNCONFIGURED;
	}
	struct pollfd pfd;
	pfd.fd = fd;
	pfd.events = 0;
	if (p_type == POLL_TYPE_IN || p_type == POLL_TYPE_IN_OUT) {
		pfd.events |= POLLIN;
	}
	if (p_type == POLL_TYPE_OUT || p_type == POLL_TYPE_IN_OUT) {
		pfd.events |= POLLOUT;
	}
	pfd.revents = 0;
	int result = ::poll(&pfd, 1, timeout);
	if (result < 0) {
		return ERR_BUG;
	}
	if (result == 0) {
		return ERR_BUSY;
	}
	return OK;
}

Error NetSocketWASI::recv(uint8_t *p_buffer, int p_len, int &r_read) {
	if (fd < 0) {
		return ERR_UNCONFIGURED;
	}
	ssize_t result = ::read(fd, p_buffer, p_len);
	if (result < 0) {
		r_read = 0;
		return (errno == EAGAIN) ? ERR_BUSY : ERR_FILE_CANT_READ;
	}
	r_read = (int)result;
	return OK;
}

Error NetSocketWASI::recvfrom(uint8_t *p_buffer, int p_len, int &r_read, IPAddress &r_ip, uint16_t &r_port, bool p_peek) {
	return recv(p_buffer, p_len, r_read);
}

Error NetSocketWASI::send(const uint8_t *p_buffer, int p_len, int &r_sent) {
	if (fd < 0) {
		return ERR_UNCONFIGURED;
	}
	ssize_t result = ::write(fd, p_buffer, p_len);
	if (result < 0) {
		r_sent = 0;
		return (errno == EAGAIN) ? ERR_BUSY : ERR_FILE_CANT_WRITE;
	}
	r_sent = (int)result;
	return OK;
}

Error NetSocketWASI::sendto(const uint8_t *p_buffer, int p_len, int &r_sent, IPAddress p_ip, uint16_t p_port) {
	return send(p_buffer, p_len, r_sent);
}

Ref<NetSocket> NetSocketWASI::accept(Address &r_addr) {
	if (fd < 0 || !listening) {
		return Ref<NetSocket>();
	}
	__wasi_fd_t accepted = 0;
	__wasi_errno_t err = sock_accept((__wasi_fd_t)fd, __WASI_FDFLAGS_NONBLOCK, &accepted);
	if (err != __WASI_ERRNO_SUCCESS) {
		return Ref<NetSocket>();
	}
	Ref<NetSocketWASI> socket = memnew(NetSocketWASI);
	socket->fd = (int)accepted;
	socket->listening = false;
	return Ref<NetSocket>(socket);
}

int NetSocketWASI::get_available_bytes() const {
	if (fd < 0) {
		return 0;
	}
	struct pollfd pfd;
	pfd.fd = fd;
	pfd.events = POLLIN;
	pfd.revents = 0;
	int result = ::poll(&pfd, 1, 0);
	if (result <= 0) {
		return 0;
	}
	return pfd.revents & POLLIN ? 1 : 0;
}

Error NetSocketWASI::get_socket_address(Address *r_addr) const {
	return ERR_UNAVAILABLE;
}

void NetSocketWASI::set_blocking_enabled(bool p_enabled) {
	if (fd < 0) {
		return;
	}
	__wasi_fd_fdstat_set_flags((__wasi_fd_t)fd, p_enabled ? 0 : __WASI_FDFLAGS_NONBLOCK);
}
