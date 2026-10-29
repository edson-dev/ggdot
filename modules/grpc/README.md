# Native gRPC module

Engine-level gRPC client: real HTTP/2 (not gRPC-Web), raw protobuf bytes and
reflection-driven protobuf JSON. Replaces `server/addons/net/grpc` (Go
GDExtension) for native builds.

## Classes

| Class | Purpose |
|---|---|
| `GrpcClient` | Target configuration, blocking compatibility methods, async factory |
| `GrpcCall` | One async unary or server-streaming call |
| `GrpcRequest` | Node wrapper (`HTTPRequest`-style), runs on a worker thread |
| `GrpcStream` | Bidi / client-streaming call |

## Blocking API (Go addon compatible)

```gdscript
var client := GrpcClient.new()
client.connect_server("127.0.0.1:5051", false, "")

var login := JSON.parse_string(client.invoke_json(
    "/auth.AuthService/Login",
    JSON.stringify({"username": "alpha", "password": "alpha"}),
    "{}", 8000))

var token: String = login.data.token
var raw: PackedByteArray = client.invoke(
    "/auth.AuthService/Session", PackedByteArray(),
    JSON.stringify({"authorization": "Bearer " + token}), 8000)
print(client.last_status())
```

JSON calls use gRPC server reflection (`grpc.reflection.v1alpha`, falling back
to `v1`) to fetch descriptors, then convert JSON <-> protobuf. Descriptors are
cached per client.

## Async API

```gdscript
var call := client.call_async("/service.service/Read", bytes, metadata, 8000)
call.message_received.connect(func(message): print(message))
await call.completed
print(call.get_status())

var request := GrpcRequest.new()
add_child(request)
request.request("127.0.0.1:5051", "/auth.AuthService/Session", bytes, metadata, 8000)
await request.request_completed

var stream := client.open_stream("/pkg.Service/Watch", PackedByteArray(), {}, 0)
stream.send(first_message)
stream.close_send()
await stream.closed
```

The exchange always runs on a `Thread`; results are delivered with
`call_deferred`, so the main loop is never blocked.

## Implementation

- `grpc_transport.*` — blocking TCP/TLS socket with deadlines
  (`StreamPeerTCP`/`StreamPeerTLS`).
- `grpc_h2.*` — one HTTP/2 connection per call, driven by vendored nghttp2
  (`thirdparty/nghttp2`). nghttp2 owns HPACK, flow control, SETTINGS/PING and
  framing; `grpc_h2` implements the gRPC message framing, headers, trailers,
  status and the outbound message queue for client streaming.
- `proto/grpc_binary.h` — protobuf wire reader/writer.
- `proto/grpc_descriptor.*` — `FileDescriptorProto` parser and symbol table.
- `proto/grpc_json.*` — protobuf JSON mapping, including
  `google.protobuf.Struct`/`Value`/`ListValue`/`Empty`.

## Limits

- TLS does not set ALPN `h2` yet (Godot's TLS wrapper does not expose it), so
  use plaintext h2c (gok) or terminate TLS in a proxy.
- Compressed responses are rejected; `google.protobuf.Any` and proto2 groups
  are not implemented.
- The module is disabled for `platform=wasi` (`config.py`): WASI workers have
  no outbound network.
- C++ tests live in `tests/` when `tests=yes`; the GDScript smoke tests are
  `server/tests/grpc.gd` and `server/tests/grpc.tscn`.
