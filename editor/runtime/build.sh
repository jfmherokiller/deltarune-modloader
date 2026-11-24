#!/usr/bin/env bash
# Linux counterpart of build.bat: clang-cl + lld-link. `build.sh test` builds the four native
# test executables and runs them under wine (WINE=... to pick another binary).
cd "$(dirname "$0")"
source ../../tools/clang-cl-env.sh
mkdir -p build
CXX=(clang-cl "${CLCOMMON[@]}" /EHsc /O2 /MD /W4 /DNOMINMAX /Fobuild/)
LIBS=(user32.lib advapi32.lib ws2_32.lib bcrypt.lib)
if [[ "${1:-}" == test ]]; then
    WINE=${WINE:-wine}
    run() { echo "== $1"; WINEDEBUG=-all "$WINE" "build/$1"; }
    "${CXX[@]}" /std:c++20 tests/native.cpp source/RuntimeBridge.cpp source/TcpRelay.cpp source/InspectorBridge.cpp \
        source/InspectorProtocol.cpp /Fe:build/native-tests.exe /link "${LINKCOMMON[@]}" "${LIBS[@]}"
    run native-tests.exe
    "${CXX[@]}" /std:c++20 tests/inspector.cpp source/InspectorProtocol.cpp source/InspectorValue.cpp \
        /Fe:build/inspector-tests.exe /link "${LINKCOMMON[@]}"
    run inspector-tests.exe
    "${CXX[@]}" /std:c++20 tests/inspector-transport.cpp source/InspectorBridge.cpp source/InspectorProtocol.cpp \
        source/RuntimeBridge.cpp source/TcpRelay.cpp /Fe:build/inspector-transport-tests.exe /link "${LINKCOMMON[@]}" "${LIBS[@]}"
    run inspector-transport-tests.exe
    "${CXX[@]}" /std:c++20 /I tests/fake tests/inspector-sampler.cpp source/InspectorSampler.cpp source/InspectorValue.cpp \
        source/InspectorProtocol.cpp source/RuntimeBridge.cpp /Fe:build/inspector-sampler-tests.exe /link "${LINKCOMMON[@]}" advapi32.lib
    run inspector-sampler-tests.exe
    exit 0
fi
# Stage CURRENT fork headers only within editor/runtime; do not mutate the sample plugin.
YY=../../external/YYToolkit/YYToolkit
[[ -f "$YY/source/YYTK/Shared/YYTK_Shared.hpp" ]] || { echo "YYToolkit fork missing" >&2; exit 1; }
mkdir -p build/include/YYToolkit build/include/Aurie build/include/FunctionWrapper
cp "$YY"/source/YYTK/Shared/YYTK_Shared* build/include/YYToolkit/
cp "$YY/include/Aurie/shared.hpp" build/include/Aurie/
cp "$YY/include/FunctionWrapper/FunctionWrapper.hpp" build/include/FunctionWrapper/
git -C ../../external/YYToolkit rev-parse HEAD > build/YYTK_SOURCE_COMMIT.txt
"${CXX[@]}" /std:c++latest /LD /DNDEBUG /DYYTK_DEFINE_INTERNAL=0 /I build/include \
    source/ModuleMain.cpp source/RuntimeBridge.cpp source/InspectorBridge.cpp source/InspectorProtocol.cpp \
    source/InspectorValue.cpp source/InspectorSampler.cpp source/TcpRelay.cpp build/include/YYToolkit/YYTK_Shared_Types.cpp \
    /link "${LINKCOMMON[@]}" /OUT:build/RunnerEditor.dll /IMPLIB:build/RunnerEditor.lib /PDB:build/RunnerEditor.pdb \
    /DEBUG /INCREMENTAL:NO "${LIBS[@]}"
echo "Built editor/runtime/build/RunnerEditor.dll - NOT INSTALLED"
