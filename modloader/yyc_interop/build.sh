#!/usr/bin/env bash
# Builds the example mod build/YYCHookDemo.dll. yyc_interop.hpp itself is header-only:
# add include/ to a plugin's include path to use it (build_yytk_plugin.sh does).
exec "$(dirname "$0")/../build_yytk_plugin.sh" "$(dirname "$0")" YYCHookDemo example/HookDemo.cpp
