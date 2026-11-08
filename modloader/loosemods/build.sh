#!/usr/bin/env bash
# Builds build/LooseMods.dll (the loose-mod Aurie module; see README.md).
exec "$(dirname "$0")/../build_yytk_plugin.sh" "$(dirname "$0")" LooseMods source/ModuleMain.cpp
