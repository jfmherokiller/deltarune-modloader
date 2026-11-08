#!/usr/bin/env bash
# Builds the example mods build/YYCHookDemo.dll (YYC) and build/ScriptHookDemo.dll (both).
# yyc_interop.hpp and vm_hooks.hpp are header-only: add include/ to a plugin's include path
# to use them (build_yytk_plugin.sh does).
set -euo pipefail
B="$(dirname "$0")/../build_yytk_plugin.sh"
"$B" "$(dirname "$0")" YYCHookDemo example/HookDemo.cpp
"$B" "$(dirname "$0")" ScriptHookDemo example/ScriptHookDemo.cpp
