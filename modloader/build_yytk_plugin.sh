#!/usr/bin/env bash
# Shared clang-cl build for small YYToolkit v5 plugins (see docs/linux.md).
# usage: modloader/build_yytk_plugin.sh <plugin dir> <OutName> <source.cpp>...
# Builds <plugin dir>/build/<OutName>.dll with yyc_interop/include and modloader/include on the include path.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DIR="$1"; NAME="$2"; shift 2
source "$ROOT/tools/clang-cl-env.sh"
YY="$ROOT/external/YYToolkit/YYToolkit"
cd "$DIR"
mkdir -p build
clang-cl "${CLCOMMON[@]}" /std:c++latest /EHa /O2 /MD /W3 /LD /DNDEBUG /DYYTK_DEFINE_INTERNAL=1 /Fobuild/ \
    /I "$ROOT/modloader/yyc_interop/include" /I "$YY/include" /I "$YY/source/YYTK/Shared" \
    /I "$ROOT/modloader/yytk_probe/stub" /I "$ROOT/modloader/include" \
    "$@" "$YY/source/YYTK/Shared/YYTK_Shared_Types.cpp" \
    /link "${LINKCOMMON[@]}" /OUT:"build/$NAME.dll" /IMPLIB:"build/$NAME.lib" \
    /PDB:"build/$NAME.pdb" /DEBUG /INCREMENTAL:NO user32.lib
echo "Built $DIR/build/$NAME.dll"
