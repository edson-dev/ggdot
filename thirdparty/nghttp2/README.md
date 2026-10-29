# nghttp2 (vendored)

HTTP/2 framing, HPACK and flow control for the built-in `modules/grpc`.

- Upstream: https://github.com/nghttp2/nghttp2
- Version: 1.68.0
- License: MIT (see `COPYING` / `LICENSE`)

Only the client-side library sources are vendored (`lib/*.c`, `lib/*.h` and
`include/nghttp2/nghttp2.h`). `nghttp2ver.h` is generated with the release
version; the upstream build generates it from `nghttp2ver.h.in`.

Local changes:

- `include/nghttp2/nghttp2.h` defines `ssize_t` as `ptrdiff_t` on MSVC, which
  is what upstream's `config.h` would do. Godot builds nghttp2 without a
  configure step, so the typedef lives in the header.
- Compiled with `NGHTTP2_STATICLIB` and without `HAVE_CONFIG_H`.

To update, replace `lib/`, `include/` and the version header from a newer
release and rebuild; no API changes are expected in the client-only surface
used by `modules/grpc/grpc_h2.cpp`.
