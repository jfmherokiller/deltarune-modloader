#!/usr/bin/env bash
# Builds compiler/RunnerCompiler/build/RunnerCompiler.dll (YYToolkit v5 plugin, SPIKE) and the
# clrhost-test host (Windows .exe via clang-cl, plus a native Linux build when g++ is present).
cd "$(dirname "$0")"
source ../../tools/clang-cl-env.sh
YY=../../external/YYToolkit/YYToolkit
mkdir -p build/include/YYToolkit build/include/Aurie build/include/FunctionWrapper
cp "$YY"/source/YYTK/Shared/YYTK_Shared* build/include/YYToolkit/
cp "$YY/include/Aurie/shared.hpp" build/include/Aurie/
cp "$YY/include/FunctionWrapper/FunctionWrapper.hpp" build/include/FunctionWrapper/
CXX=(clang-cl "${CLCOMMON[@]}" /std:c++latest /EHsc /O2 /MD /W3 /DNDEBUG /DNOMINMAX /Fobuild/)
"${CXX[@]}" /LD /DYYTK_DEFINE_INTERNAL=0 /I build/include /I native /I ../../modloader/include \
    native/ModuleMain.cpp native/ClrHost.cpp build/include/YYToolkit/YYTK_Shared_Types.cpp \
    /link "${LINKCOMMON[@]}" /OUT:build/RunnerCompiler.dll /IMPLIB:build/RunnerCompiler.lib /INCREMENTAL:NO user32.lib shell32.lib
"${CXX[@]}" native/ClrHost.cpp tests/clrhost-test.cpp /Fe:build/clrhost-test.exe /link "${LINKCOMMON[@]}"
if command -v g++ >/dev/null; then g++ -std=c++20 -O1 -Wall native/ClrHost.cpp tests/clrhost-test.cpp -ldl -o build/clrhost-test; fi
echo "Built compiler/RunnerCompiler/build/RunnerCompiler.dll - NOT INSTALLED"
