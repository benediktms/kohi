#!/bin/bash
# Build script for cleaning and/or building everything
PLATFORM="$1"
ACTION="$2"
TARGET="$3"

set echo off

txtgrn=$(echo -e '\e[0;32m')
txtred=$(echo -e '\e[0;31m')
txtrst=$(echo -e '\e[0m')

if [ $ACTION = "all" ] || [ $ACTION = "build" ]
then
   ACTION="all"
   ACTION_STR="Building"
   ACTION_STR_PAST="built"
   DO_VERSION="yes"
elif [ $ACTION = "clean" ]
then
   ACTION="clean"
   ACTION_STR="Cleaning"
   ACTION_STR_PAST="cleaned"
   DO_VERSION="no"
else
   echo "Unknown action $ACTION. Aborting" && exit
fi

INC_CORE_RT="-I./kohi.core/src -I./kohi.runtime/src"
LNK_CORE_RT="-lkohi.core -lkohi.runtime"

echo "$ACTION_STR everything on $PLATFORM ($TARGET)..."

# Version Generator - Build this first so it can be used later in the build process.
make -f Makefile.kohi.mak $ACTION TARGET=$TARGET ASSEMBLY=kohi.tools.versiongen BUILD_MODE=exe DO_VERSION=no FOLDER=
ERRORLEVEL=$?
if [ $ERRORLEVEL -ne 0 ]
then
echo "error:"$errorlevel | sed -e "s/error/${txtred}error${txtrst}/g" && exit
fi

# Kohi Core
ADDL_CORE_LINK_FLAGS=
if [ $PLATFORM = 'macos' ]
then
    ADDL_CORE_LINK_FLAGS="-lobjc -framework AppKit -framework QuartzCore -framework DiskArbitration -framework CoreFoundation"
else
    ADDL_CORE_LINK_FLAGS="-lxcb -lX11 -lX11-xcb -lxkbcommon -lxcb-xkb -lm -ldl -L/usr/X11R6/lib"
fi
make -f Makefile.kohi.mak $ACTION TARGET=$TARGET ASSEMBLY=kohi.core BUILD_MODE=lib DO_VERSION=$DO_VERSION ADDL_LINK_FLAGS="$ADDL_CORE_LINK_FLAGS" FOLDER=
ERRORLEVEL=$?
if [ $ERRORLEVEL -ne 0 ]
then
echo "error:"$errorlevel | sed -e "s/error/${txtred}error${txtrst}/g" && exit
fi

ASSIMP_INC=
ASSIMP_LIB=
if [ $PLATFORM = 'macos' ]
then
    ASSIMP_INC="/opt/homebrew/Cellar/assimp/6.0.2/include/"
    ASSIMP_LIB="/opt/homebrew/lib/assimp/"
else
    ASSIMP_INC="/usr/include/assimp/"
    ASSIMP_LIB="/usr/lib/"
fi

# Tools NOTE: Building tools here since it's required below.
make -f Makefile.kohi.mak $ACTION TARGET=$TARGET ASSEMBLY=kohi.tools BUILD_MODE=exe ADDL_INC_FLAGS="$INC_CORE_RT -I$ASSIMP_INC" ADDL_LINK_FLAGS="-lm -lkohi.core -L$ASSIMP_LIB -lassimp " FOLDER=
ERRORLEVEL=$?
if [ $ERRORLEVEL -ne 0 ]
then
echo "Error:"$ERRORLEVEL | sed -e "s/Error/${txtred}Error${txtrst}/g" && exit
fi

# Kohi Runtime
make -f Makefile.kohi.mak $ACTION TARGET=$TARGET ASSEMBLY=kohi.runtime BUILD_MODE=lib DO_VERSION=$DO_VERSION ADDL_INC_FLAGS="$INC_CORE_RT" ADDL_LINK_FLAGS="-lm -lkohi.core" FOLDER=
ERRORLEVEL=$?
if [ $ERRORLEVEL -ne 0 ]
then
echo "error:"$errorlevel | sed -e "s/error/${txtred}error${txtrst}/g" && exit
fi

# Vulkan Renderer Lib
if [ $PLATFORM = 'macos' ]
then
   VULKAN_SDK=/usr/local/
fi
make -f Makefile.kohi.mak $ACTION TARGET=$TARGET ASSEMBLY=kohi.plugin.renderer.vulkan BUILD_MODE=lib DO_VERSION=$DO_VERSION ADDL_INC_FLAGS="$INC_CORE_RT -I../bin/ -I$VULKAN_SDK/include" ADDL_LINK_FLAGS="$LNK_CORE_RT -lshaderc_shared " FOLDER=
ERRORLEVEL=$?
if [ $ERRORLEVEL -ne 0 ]
then
echo "error:"$errorlevel | sed -e "s/error/${txtred}error${txtrst}/g" && exit
fi

# Standard UI Lib
make -f Makefile.kohi.mak $ACTION TARGET=$TARGET ASSEMBLY=kohi.plugin.ui.standard BUILD_MODE=lib DO_VERSION=$DO_VERSION ADDL_INC_FLAGS="$INC_CORE_RT" ADDL_LINK_FLAGS="$LNK_CORE_RT" FOLDER=
ERRORLEVEL=$?
if [ $ERRORLEVEL -ne 0 ]
then
echo "error:"$errorlevel | sed -e "s/error/${txtred}error${txtrst}/g" && exit
fi

# Kohi Utils plugin Lib
make -f Makefile.kohi.mak $ACTION TARGET=$TARGET ASSEMBLY=kohi.plugin.utils BUILD_MODE=lib DO_VERSION=$DO_VERSION ADDL_INC_FLAGS="$INC_CORE_RT -I./kohi.plugin.ui.standard/src" ADDL_LINK_FLAGS="-lm $LNK_CORE_RT -lkohi.plugin.ui.standard" FOLDER=
ERRORLEVEL=$?
if [ $ERRORLEVEL -ne 0 ]
then
echo "error:"$errorlevel | sed -e "s/error/${txtred}error${txtrst}/g" && exit
fi

# OpenAL Audio Plugin lib
if [ $PLATFORM = 'macos' ]
then
    OPENAL_INC=-I/opt/homebrew/opt/openal-soft/include/
    OPENAL_LIB=-L/opt/homebrew/opt/openal-soft/lib/
fi
make -f Makefile.kohi.mak $ACTION TARGET=$TARGET ASSEMBLY=kohi.plugin.audio.openal BUILD_MODE=lib DO_VERSION=$DO_VERSION ADDL_INC_FLAGS="$INC_CORE_RT $OPENAL_INC" ADDL_LINK_FLAGS="$LNK_CORE_RT -lopenal $OPENAL_LIB" FOLDER=
ERRORLEVEL=$?
if [ $ERRORLEVEL -ne 0 ]
then
echo "error:"$errorlevel | sed -e "s/error/${txtred}error${txtrst}/g" && exit
fi

# Testbed Lib
make -f Makefile.kohi.mak $ACTION TARGET=$TARGET ASSEMBLY=testbed.klib BUILD_MODE=lib DO_VERSION=$DO_VERSION ADDL_INC_FLAGS="$INC_CORE_RT -I./kohi.plugin.ui.standard/src -I./kohi.plugin.audio.openal/src -I./kohi.plugin.utils/src" ADDL_LINK_FLAGS="$LNK_CORE_RT -lkohi.plugin.ui.standard -lkohi.plugin.audio.openal -lkohi.plugin.utils" FOLDER=
ERRORLEVEL=$?
if [ $ERRORLEVEL -ne 0 ]
then
echo "Error:"$ERRORLEVEL | sed -e "s/Error/${txtred}Error${txtrst}/g" && exit
fi


# ---------------------------------------------------
# Executables
# ---------------------------------------------------

# Testbed
make -f Makefile.kohi.mak $ACTION TARGET=$TARGET ASSEMBLY=testbed.kapp BUILD_MODE=exe ADDL_INC_FLAGS="$INC_CORE_RT" ADDL_LINK_FLAGS="$LNK_CORE_RT -lkohi.plugin.ui.standard -lkohi.plugin.audio.openal" FOLDER=
ERRORLEVEL=$?
if [ $ERRORLEVEL -ne 0 ]
then
echo "Error:"$ERRORLEVEL | sed -e "s/Error/${txtred}Error${txtrst}/g" && exit
fi

# Core Tests
make -f Makefile.kohi.mak $ACTION TARGET=$TARGET ASSEMBLY=kohi.core.tests BUILD_MODE=exe ADDL_INC_FLAGS="$INC_CORE_RT" ADDL_LINK_FLAGS="$LNK_CORE_RT" FOLDER=
ERRORLEVEL=$?
if [ $ERRORLEVEL -ne 0 ]
then
echo "Error:"$ERRORLEVEL | sed -e "s/Error/${txtred}Error${txtrst}/g" && exit
fi

# Runtime Tests
make -f Makefile.kohi.mak $ACTION TARGET=$TARGET ASSEMBLY=kohi.runtime.tests BUILD_MODE=exe ADDL_INC_FLAGS="$INC_CORE_RT" ADDL_LINK_FLAGS="$LNK_CORE_RT" FOLDER=
ERRORLEVEL=$?
if [ $ERRORLEVEL -ne 0 ]
then
echo "Error:"$ERRORLEVEL | sed -e "s/Error/${txtred}Error${txtrst}/g" && exit
fi


echo "All assemblies $ACTION_STR_PAST successfully on $PLATFORM ($TARGET)." | sed -e "s/successfully/${txtgrn}successfully${txtrst}/g"

