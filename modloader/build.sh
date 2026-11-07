#!/usr/bin/env bash
# Linux counterpart of build.bat: builds modloader/build/version.dll with clang-cl + lld-link.
cd "$(dirname "$0")"
source ../tools/clang-cl-env.sh
mkdir -p build
MH=third_party/minhook
clang-cl "${CLCOMMON[@]}" /O2 /MT /W3 /GS /LD /D_CRT_SECURE_NO_WARNINGS /Fobuild/ /I "$MH/include" \
    src/modloader.c src/exports.c "$MH/src/hook.c" "$MH/src/buffer.c" "$MH/src/trampoline.c" "$MH/src/hde/hde64.c" \
    /link "${LINKCOMMON[@]}" /DEF:src/version.def /OUT:build/version.dll /PDB:build/version.pdb \
    /IMPLIB:build/version.lib /DEBUG /INCREMENTAL:NO kernel32.lib
echo "Built build/version.dll"
