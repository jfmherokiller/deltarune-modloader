#!/usr/bin/env bash
# Linux counterpart of build.bat: builds build/DeltaruneYYTK.dll (YYToolkit v5 plugin).
cd "$(dirname "$0")"
source ../../tools/clang-cl-env.sh
mkdir -p build
clang-cl "${CLCOMMON[@]}" /std:c++latest /EHsc /O2 /MD /W3 /LD /DNDEBUG /DYYTK_DEFINE_INTERNAL=0 /Fobuild/ /I include \
    source/ModuleMain.cpp include/YYToolkit/YYTK_Shared_Types.cpp \
    /link "${LINKCOMMON[@]}" /OUT:build/DeltaruneYYTK.dll /IMPLIB:build/DeltaruneYYTK.lib /PDB:build/DeltaruneYYTK.pdb \
    /DEBUG /INCREMENTAL:NO user32.lib
echo "Built build/DeltaruneYYTK.dll"
