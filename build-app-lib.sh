#!/bin/bash

txtgrn=$(echo -e '\e[0;32m')
txtred=$(echo -e '\e[0;31m')
txtrst=$(echo -e '\e[0m')

INC_CORE_RT="-I./kohi.core/src -I./kohi.runtime/src"
LNK_CORE_RT="-lkohi.core -lkohi.runtime"

# Testbed Lib
make -f Makefile.kohi.mak $ACTION TARGET=$TARGET ASSEMBLY=testbed.klib BUILD_MODE=lib DO_VERSION=$DO_VERSION ADDL_INC_FLAGS="$INC_CORE_RT -I./kohi.plugin.ui.standard/src -I./kohi.plugin.audio.openal/src -I./kohi.plugin.utils/src" ADDL_LINK_FLAGS="$LNK_CORE_RT -lkohi.plugin.ui.standard -lkohi.plugin.audio.openal -lkohi.plugin.utils"
ERRORLEVEL=$?
if [ $ERRORLEVEL -ne 0 ]
then
echo "Error:"$ERRORLEVEL | sed -e "s/Error/${txtred}Error${txtrst}/g" && exit
fi

