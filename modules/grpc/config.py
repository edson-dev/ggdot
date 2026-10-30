def can_build(env, platform):
    # Native, Web and WASI are supported. WASI gets outbound TCP from the
    # gok.tcp_* host module (platform/wasi/net_socket_wasi.cpp), so the gRPC
    # client can run in the sandbox as well as in native builds.
    return platform not in ["web"]


def configure(env):
    pass
