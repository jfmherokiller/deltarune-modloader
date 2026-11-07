#!/usr/bin/env bash
# Linux counterpart of the CI step "MSBuild YYToolkit.vcxproj Release|x64": builds the fork submodule
# (external/YYToolkit) to out/yytk/YYToolkit.dll with clang-cl. Sources and defines mirror the
# vcxproj; keep them in sync when the submodule's project file changes.
cd "$(dirname "$0")/.."
source tools/clang-cl-env.sh
YY=external/YYToolkit/YYToolkit
[[ -f "$YY/YYToolkit.vcxproj" ]] || { echo "YYToolkit submodule missing: git submodule update --init" >&2; exit 1; }
OUT=${1:-out/yytk}
mkdir -p "$OUT/obj"
mapfile -t SOURCES < <(grep -o 'ClCompile Include="[^"]*"' "$YY/YYToolkit.vcxproj" | sed 's/.*Include="//; s/"$//; s#\\#/#g')
CPP=() C=()
for s in "${SOURCES[@]}"; do [[ $s == *.c ]] && C+=("$YY/$s") || CPP+=("$YY/$s"); done
DEFS=(/DYYTK_INCLUDE_PRIVATE /DYYTK_DEFINE_INTERNAL /DNDEBUG /D_CONSOLE /DUNICODE /D_UNICODE /D_WINDLL)
clang-cl "${CLCOMMON[@]}" /c /O2 /MD /W3 "${DEFS[@]}" /I "$YY/include" /Fo"$OUT/obj/" "${C[@]}"
clang-cl "${CLCOMMON[@]}" /c /std:c++latest /EHsc /O2 /MD /W3 "${DEFS[@]}" /I "$YY/include" /Fo"$OUT/obj/" "${CPP[@]}"
lld-link "${LINKCOMMON[@]}" /DLL /OUT:"$OUT/YYToolkit.dll" /IMPLIB:"$OUT/YYToolkit.lib" /PDB:"$OUT/YYToolkit.pdb" \
    /DEBUG /INCREMENTAL:NO "$OUT"/obj/*.obj kernel32.lib user32.lib
echo "Built $OUT/YYToolkit.dll"
