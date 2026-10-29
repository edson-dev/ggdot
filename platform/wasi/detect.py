import os
import shutil
import sys
from pathlib import Path
from typing import TYPE_CHECKING

from SCons.Script import ARGUMENTS
from SCons.Variables import BoolVariable

from methods import print_error

if TYPE_CHECKING:
    from SCons.Script.SConscript import SConsEnvironment


def get_name():
    return "WASI"


def _sdk_root():
    """Locate the WASI SDK (clang + wasm32-wasip1 sysroot)."""
    candidates = []
    arg = ARGUMENTS.get("wasi_sdk", "")
    if arg:
        candidates.append(arg)
    env_path = os.environ.get("WASI_SDK_PATH", "")
    if env_path:
        candidates.append(env_path)
    # Common local layout used by scripts/build_wasi.sh.
    candidates.append(str(Path("thirdparty/wasi-sdk").resolve()))

    for candidate in candidates:
        root = Path(candidate)
        if (root / "bin" / "clang.exe").exists() or (root / "bin" / "clang").exists():
            return root
    return None


def can_build():
    return _sdk_root() is not None


def get_tools(env: "SConsEnvironment"):
    # Use the generic POSIX-style toolchain; detect.py points CC/CXX/AR at the
    # WASI SDK (clang cross-compiling to wasm32-wasip1).
    return ["cc", "c++", "ar", "link", "textfile", "zip"]


def get_opts():
    return [
        ("wasi_sdk", "Path to the WASI SDK", os.environ.get("WASI_SDK_PATH", "")),
        ("wasi_stack_size", "WASM stack size in bytes", 8 * 1024 * 1024),
        ("wasi_initial_memory", "Initial WASM memory in MiB", 64),
        ("wasi_max_memory", "Maximum WASM memory in MiB (0 = no maximum)", 4096),
        BoolVariable("wasi_export_all", "Export all symbols (debugging aid)", False),
    ]


def get_flags():
    # A WASI build is always a template: there is no editor and no renderer.
    return {
        "arch": "wasm32",
        "tools": False,
        "disable_3d": True,
        "use_llvm": True,
        "threads": False,
        "vulkan": False,
        "opengl3": False,
        "rendering_device": False,
        "d3d12": False,
        "metal": False,
        "x11": False,
        "wayland": False,
        "sdl": False,
        "dbus": False,
        "fontconfig": False,
        "speechd": False,
        "pulseaudio": False,
        "alsa": False,
        "use_volk": False,
        "accesskit": False,
        "use_sowrap": False,
        "module_text_server_adv_enabled": False,
        "module_glslang_enabled": False,
        "module_theora_enabled": False,
        "module_vorbis_enabled": False,
        "module_ogg_enabled": False,
        "module_webxr_enabled": False,
        "module_openxr_enabled": False,
        "module_fbx_enabled": False,
        "module_basis_universal_enabled": False,
        "module_svg_enabled": False,
        "module_astcenc_enabled": False,
        "module_jpg_enabled": False,
        "module_lightmapper_rd_enabled": False,
        "module_gltf_enabled": False,
        "module_csg_enabled": False,
        "module_gridmap_enabled": False,
        "module_tilemap_enabled": False,
        "module_visual_shader_enabled": False,
        "module_mbedtls_enabled": False,
        "module_raycast_enabled": False,
        "module_upnp_enabled": False,
        "module_webrtc_enabled": False,
        "module_tinyexr_enabled": False,
        "builtin_pcre2_with_jit": False,
        "module_zip_enabled": False,
        "minizip": False,
        "disable_path_overrides": False,
        "module_freetype_enabled": False,
        "module_interactive_music_enabled": False,
        "module_mp3_enabled": False,
        "module_noise_enabled": False,
    }


def configure(env: "SConsEnvironment"):
    sdk = _sdk_root()
    if sdk is None:
        print_error("WASI SDK not found. Set WASI_SDK_PATH or pass wasi_sdk=/path/to/wasi-sdk.")
        sys.exit(255)

    bin_dir = sdk / "bin"
    sysroot = sdk / "share" / "wasi-sysroot"
    if not sysroot.exists():
        print_error(f"WASI sysroot not found at {sysroot}.")
        sys.exit(255)

    def tool(name):
        candidate = bin_dir / (name + (".exe" if os.name == "nt" else ""))
        return str(candidate) if candidate.exists() else str(bin_dir / name)

    env["CC"] = tool("clang")
    env["CXX"] = tool("clang++")
    env["AR"] = tool("llvm-ar")
    env["RANLIB"] = tool("llvm-ranlib")
    env["LINK"] = env["CXX"]

    env["PROGSUFFIX"] = ".wasm"
    env["LIBSUFFIX"] = ".a"
    env["SHLIBSUFFIX"] = ".wasm"

    env.Prepend(CPPPATH=["#platform/wasi", "#platform/wasi/compat"])

    target_flags = ["--target=wasm32-wasip1", f"--sysroot={sysroot.as_posix()}"]
    env.Append(CCFLAGS=target_flags)
    env.Append(CXXFLAGS=target_flags)
    env.Append(LINKFLAGS=target_flags)

    env.Append(CPPDEFINES=["WASI_ENABLED", "UNIX_ENABLED", "UNIX_SOCKET_UNAVAILABLE", "NO_THREADS"])
    env.Append(CPPDEFINES=["_WASI_EMULATED_SIGNAL", "_WASI_EMULATED_PROCESS_CLOCKS"])

    # llvm-ar command lines exceed the Windows command-line limit for big
    # libraries (e.g. module_webp); use a response file like the MSVC path does.
    env["ARCOM_ORIG"] = env["ARCOM"]
    env["ARCOM"] = "${TEMPFILE('$ARCOM_ORIG', '$ARCOMSTR')}"
    env["TEMPFILESUFFIX"] = ".rsp"
    env.Append(LIBS=["wasi-emulated-signal", "wasi-emulated-process-clocks"])

    env.Append(CCFLAGS=["-fno-exceptions", "-fvisibility=hidden", "-fno-strict-aliasing"])
    env.Append(CXXFLAGS=["-std=c++17"])

    if env["target"] == "template_debug":
        env.Append(CCFLAGS=["-g", "-O1"])
        env.Append(LINKFLAGS=["-Wl,--export-memory"])
    else:
        env.Append(CCFLAGS=["-O2"])
        env.Append(LINKFLAGS=["-Wl,--strip-debug"])

    stack_size = int(env.get("wasi_stack_size", 8 * 1024 * 1024))
    initial_memory = int(env.get("wasi_initial_memory", 64)) * 1024 * 1024
    max_memory = int(env.get("wasi_max_memory", 4096)) * 1024 * 1024

    env.Append(
        LINKFLAGS=[
            # Command module: WASI libc's crt1-command provides `_start`.
            "-mexec-model=command",
            "-Wl,--export=_start",
            f"-Wl,-z,stack-size={stack_size}",
            f"-Wl,--initial-memory={initial_memory}",
            "-Wl,--gc-sections",
        ]
    )
    if max_memory > 0:
        env.Append(LINKFLAGS=[f"-Wl,--max-memory={max_memory}"])
    if env.get("wasi_export_all", False):
        env.Append(LINKFLAGS=["-Wl,--export-all"])
