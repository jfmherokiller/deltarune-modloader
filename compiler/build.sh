#!/usr/bin/env bash
# Builds compiler/build/win-x64/LooseModCompiler.exe (self-contained, trimmed, ReadyToRun).
# Install: copy compiler/build/win-x64/* to <game>/mods/tools/compiler/. Needs the .NET 10 SDK and
# `git submodule update --init --recursive` (external/UndertaleModTool + Underanalyzer).
# `build.sh linux-x64` builds a native Linux copy (for tests / Linux modding workflows).
set -euo pipefail
cd "$(dirname "$0")"
RID=${1:-win-x64}
[[ -f ../external/UndertaleModTool/UndertaleModLib/UndertaleModLib.csproj ]] ||
    { echo "submodule missing: git submodule update --init --recursive" >&2; exit 1; }
rm -rf "build/$RID"
dotnet publish LooseModCompiler/LooseModCompiler.csproj -c Release -r "$RID" -nologo -v q -o "build/$RID"
git -C ../external/UndertaleModTool rev-parse HEAD > "build/$RID/UNDERTALEMODTOOL_COMMIT.txt"
echo "Built compiler/build/$RID ($(du -sh "build/$RID" | cut -f1))"
