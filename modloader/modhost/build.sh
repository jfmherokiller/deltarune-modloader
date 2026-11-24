#!/usr/bin/env bash
# Builds build/ModHost.dll and the example build/HelloPlugin.dll.
# `build.sh test` builds and runs the manifest/load-order tests with the host C++ compiler.
set -euo pipefail
cd "$(dirname "$0")"
if [[ "${1:-}" == test ]]; then
    mkdir -p build
    "${CXX:-c++}" -std=c++20 -Wall -Wextra -O1 tests/resolve.cpp source/Manifest.cpp -o build/resolve-tests
    exec build/resolve-tests
fi
B=../build_yytk_plugin.sh
"$B" . ModHost /I include source/ModuleMain.cpp source/Manifest.cpp
"$B" . HelloPlugin /I include example/HelloPlugin.cpp
