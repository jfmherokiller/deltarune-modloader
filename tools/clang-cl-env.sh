# Sourced by the */build.sh scripts: clang-cl + lld-link against an MSVC sysroot,
# producing the same MSVC-ABI binaries as the build.bat files on Windows.
#
# Sysroot: $MSVC_SYSROOT, else ~/.xwin, else ~/.cache/msvc-sysroot. Create one with
#   python3 tools/msvc_sysroot.py --windows-root /mnt/c --output ~/.cache/msvc-sysroot
# (symlinks into an existing Windows VS + SDK install), or download it with
#   xwin --accept-license splat --output ~/.xwin
# Needs clang (clang-cl), lld (lld-link) and llvm (llvm-rc not required).

set -euo pipefail

if [[ -z "${MSVC_SYSROOT:-}" ]]; then
    for candidate in "$HOME/.xwin" "$HOME/.cache/msvc-sysroot"; do
        if [[ -d "$candidate/crt/include" ]]; then MSVC_SYSROOT="$candidate"; break; fi
    done
fi
if [[ -z "${MSVC_SYSROOT:-}" || ! -d "$MSVC_SYSROOT/crt/include" || ! -d "$MSVC_SYSROOT/sdk/include/um" ]]; then
    echo "MSVC sysroot not found. Set MSVC_SYSROOT, or create one with tools/msvc_sysroot.py or xwin (see tools/clang-cl-env.sh)." >&2
    exit 1
fi
for tool in clang-cl lld-link; do
    command -v "$tool" >/dev/null || { echo "$tool not found (install clang and lld)" >&2; exit 1; }
done

# Flags shared by every target. Callers append their own (/O2 /MT /LD ...).
CLCOMMON=(--target=x86_64-pc-windows-msvc -fuse-ld=lld /nologo
    -Wno-unused-command-line-argument -Wno-microsoft-cast
    /imsvc "$MSVC_SYSROOT/crt/include"
    /imsvc "$MSVC_SYSROOT/sdk/include/ucrt"
    /imsvc "$MSVC_SYSROOT/sdk/include/um"
    /imsvc "$MSVC_SYSROOT/sdk/include/shared")
LINKCOMMON=(/libpath:"$MSVC_SYSROOT/crt/lib/x86_64"
    /libpath:"$MSVC_SYSROOT/sdk/lib/um/x86_64"
    /libpath:"$MSVC_SYSROOT/sdk/lib/ucrt/x86_64")
