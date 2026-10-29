def can_build(env, platform):
    # gRPC needs a native TCP stack with HTTP/2. Neither Web (Emscripten) nor
    # WASI (no outbound sockets) can host it.
    return platform not in ["web", "wasi"]


def configure(env):
    pass
