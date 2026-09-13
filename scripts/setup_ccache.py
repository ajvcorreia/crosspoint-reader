"""Wrap the toolchain's CC/CXX with ccache when it's installed, to skip
recompiling translation units that haven't changed since the last build
(across full rebuilds too, unlike PlatformIO's own object cache, which is
keyed to a single .pio/build/<env> directory). A no-op when ccache isn't on
PATH, so this is safe on machines/CI that don't have it -- nothing here is
required for a correct build.
"""
import os
import shutil
from pathlib import Path

Import("env")  # noqa: F821 -- provided by PlatformIO
# "env" covers the framework/library build graph; PlatformIO compiles the
# project's own src/ files through "projenv", a separate clone taken before
# this post: script runs, so CC/CXX must be wrapped on both.
Import("projenv")  # noqa: F821 -- provided by PlatformIO

# A "post:" extra_script runs inside PlatformIO's own long-lived Python
# process, which can hold an environment snapshot from before ccache was
# added to PATH (e.g. right after installing it). Fall back to the known
# winget install location so this still works without restarting PlatformIO.
ccache = shutil.which("ccache") or next(
    (
        p
        for p in [
            r"C:\Users\acorreia\AppData\Local\Microsoft\WinGet\Packages\Ccache.Ccache_Microsoft.Winget.Source_8wekyb3d8bbwe\ccache-4.14-windows-x86_64\ccache.exe"
        ]
        if os.path.isfile(p)
    ),
    None,
)
if ccache:
    cache_dir = Path(env.subst("$PROJECT_DIR")) / ".cache" / "ccache"
    cache_dir.mkdir(parents=True, exist_ok=True)
    os.environ.setdefault("CCACHE_DIR", str(cache_dir))
    # The toolchain path itself changes across pioarduino package updates;
    # let ccache hash the compiler's mtime+size instead of failing open.
    os.environ.setdefault("CCACHE_COMPILERCHECK", "mtime")

    # SCons resolves $LINK to $SMARTLINK, which picks $CXX for a C++ link and
    # invokes it via a TEMPFILE()-generated response file. ccache doesn't
    # cache links anyway, so pin LINK to the unwrapped compiler first.
    env["LINK"] = env["CXX"]

    # This project's ESP-IDF include list makes ordinary compile lines exceed
    # Windows's ~32K command-line limit, so SCons *must* route them through a
    # TEMPFILE()-generated response file -- that's not avoidable here. SCons's
    # TempFileMunge keeps only the first whitespace-separated token of $CC/$CXX
    # outside the response file and puts the rest (including the real compiler
    # name, if $CC/$CXX were just "ccache <compiler>") inside it, leaving
    # ccache invoked as `ccache.exe @file` with no compiler name in its own
    # argv -- it can't recover the compiler from inside the response file, and
    # errors trying to execute "@file" itself as a program.
    # A one-line wrapper script keeps "ccache" and the real compiler bundled
    # as a single atomic token, so the response file only ever holds the
    # compiler's own arguments -- restoring the normal `ccache <compiler>
    # @file` shape ccache expects.
    def make_wrapper(real_compiler, name):
        wrapper = cache_dir / name
        wrapper.write_text(f'@echo off\r\n"{ccache}" {real_compiler} %*\r\n')
        return str(wrapper)

    env["CC"] = make_wrapper(env["CC"], "cc_wrapper.cmd")
    env["CXX"] = make_wrapper(env["CXX"], "cxx_wrapper.cmd")
    projenv["CC"] = env["CC"]
    projenv["CXX"] = env["CXX"]
