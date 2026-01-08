REM Builds only the testbed library

SET INC_CORE_RT=-Ikohi.core\src -Ikohi.runtime\src
SET LNK_CORE_RT=-lkohi.core -lkohi.runtime

REM Testbed lib
make -j -f "Makefile.kohi.mak" %ACTION% TARGET=%TARGET% ASSEMBLY=testbed.klib BUILD_MODE=lib DO_VERSION=%DO_VERSION% ADDL_INC_FLAGS="%INC_CORE_RT% -Isubmodules\kohi\kohi.plugin.ui.standard\src -Isubmodules\kohi\kohi.plugin.audio.openal\src -Isubmodules\kohi\kohi.plugin.utils\src" ADDL_LINK_FLAGS="%LNK_CORE_RT% -lkohi.plugin.ui.standard -lkohi.plugin.audio.openal -lkohi.plugin.utils"
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)
