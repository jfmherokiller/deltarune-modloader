#!/usr/bin/env bash
# Linux counterpart of build.bat: builds build/YYTKProbe.dll against the YYToolkit submodule headers.
cd "$(dirname "$0")"
source ../../tools/clang-cl-env.sh
YY=../../external/YYToolkit/YYToolkit
mkdir -p build
clang-cl "${CLCOMMON[@]}" /std:c++latest /EHa /O2 /MD /W3 /LD /DNDEBUG /DYYTK_DEFINE_INTERNAL=1 /Fobuild/ \
    /I "$YY/include" /I "$YY/source/YYTK/Shared" /I stub \
    source/ModuleMain.cpp "$YY/source/YYTK/Shared/YYTK_Shared_Types.cpp" \
    /link "${LINKCOMMON[@]}" /OUT:build/YYTKProbe.dll /IMPLIB:build/YYTKProbe.lib /PDB:build/YYTKProbe.pdb \
    /DEBUG /INCREMENTAL:NO user32.lib
echo "Built build/YYTKProbe.dll"
