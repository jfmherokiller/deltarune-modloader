#!/usr/bin/env bash
# Builds build/YYCRuntimeDump.dll (see build_yytk_plugin.sh).
exec "$(dirname "$0")/../build_yytk_plugin.sh" "$(dirname "$0")" YYCRuntimeDump source/ModuleMain.cpp
