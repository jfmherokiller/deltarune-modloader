#!/usr/bin/env bash
# Compiles the GMLO test snippets in this folder for one game.
# usage: modloader/yyc_vm_probe/gmlo/compile.sh <game data.win> <out dir>
# Builds compiler/GmlObjectCompiler (needs the .NET 10 SDK), writes <out dir>/<stem>.gmlo for every
# *.gml here and copies the .expect files next to them. Copy <out dir> to <game>/gmlo/.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
BIN="$ROOT/compiler/build/GmlObjectCompiler"
dotnet build -c Release -o "$BIN" "$ROOT/compiler/GmlObjectCompiler" > /dev/null
mkdir -p "$2"
dotnet "$BIN/GmlObjectCompiler.dll" "$1" "$2" "$HERE"/*.gml
cp "$HERE"/*.expect "$2"/
