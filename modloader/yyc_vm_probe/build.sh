#!/usr/bin/env bash
# Builds build/YYCVmProbe.dll (see build_yytk_plugin.sh).
exec "$(dirname "$0")/../build_yytk_plugin.sh" "$(dirname "$0")" YYCVmProbe source/ModuleMain.cpp
