#!/usr/bin/env bash
# export_sprites.sh <chapter 1-5> <ModName> <sprite> [sprite ...]
# Linux counterpart of export_sprites.bat: copies the game's original sprite frames into
# mods/<ModName>/chapter<N>_windows/sprites/, named and laid out the way the loader reads them.
# Wildcards work (quote them): 'spr_susie_walk_*'. Run it from anywhere; it finds the game
# folder from its own location (mods/tools/).
#
# Compiler, first found wins:
#   $UTMT_CLI                         any UndertaleModCli (native Linux build recommended)
#   utmtcli / UndertaleModCli on PATH native Linux UTMT CLI (UTMT_CLI_v0.9.2.0-Linux.zip)
#   mods/tools/utmt/UndertaleModCli.exe through wine (the copy the in-game loader uses)
set -euo pipefail
if [[ $# -lt 3 ]]; then
    echo "usage: export_sprites.sh <chapter 1-5> <ModName> <sprite> [sprite ...]"
    echo "  e.g. export_sprites.sh 2 MyMod spr_city_mice_bell"
    echo "       export_sprites.sh 1 MyMod 'spr_krisd*'"
    exit 1
fi
TOOLS=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
GAME=$(cd "$TOOLS/../.." && pwd)
CH=$1 MOD=$2
shift 2
DATA="$GAME/chapter${CH}_windows/data.win"
[[ -f "$DATA" ]] || { echo "chapter $CH not found: \"$DATA\"" >&2; exit 1; }
OUT="$GAME/mods/$MOD/chapter${CH}_windows/sprites"
SPRITES=$(IFS=';'; echo "$*;")

if [[ -n "${UTMT_CLI:-}" ]]; then CLI=("$UTMT_CLI")
elif command -v utmtcli >/dev/null; then CLI=(utmtcli)
elif command -v UndertaleModCli >/dev/null; then CLI=(UndertaleModCli)
elif [[ -f "$TOOLS/utmt/UndertaleModCli.exe" ]] && command -v "${WINE:-wine}" >/dev/null; then CLI=("${WINE:-wine}" "$TOOLS/utmt/UndertaleModCli.exe")
else
    echo "No UndertaleModCli found: set UTMT_CLI, put the Linux CLI on PATH, or install wine + mods/tools/utmt/ (see the README)" >&2
    exit 1
fi

script="$TOOLS/ExportSprites.csx" data="$DATA" out="$OUT"
if [[ "${CLI[0]}" == *wine* ]]; then
    # The Windows CLI wants Windows paths; Wine maps / to Z:.
    winpath() { printf 'Z:%s' "${1//\//\\}"; }
    script=$(winpath "$script") data=$(winpath "$data") out=$(winpath "$out")
    export WINEDEBUG=${WINEDEBUG:--all}
fi
DR_SPRITES="$SPRITES" DR_OUT_DIR="$out" "${CLI[@]}" load "$data" -s "$script" 2>&1 | grep '^\[DR\]' || true
echo
echo "Edit the PNGs in \"$OUT\" (keep the file names), then start the game."
echo "Check mods/modloader.gml.log for what was replaced."
