@ECHO OFF
REM Build script for cleaning and/or building everything

SET PLATFORM=%1
SET ACTION=%2
SET TARGET=%3


if "%ACTION%" == "build" (
    SET ACTION=all
    SET ACTION_STR=Building
    SET ACTION_STR_PAST=built
    SET DO_VERSION=yes
) else (
    if "%ACTION%" == "clean" (
        SET ACTION=clean
        SET ACTION_STR=Cleaning
        SET ACTION_STR_PAST=cleaned
        SET DO_VERSION=no
    ) else (
        echo "Unknown action %ACTION%. Aborting" && exit
    )
)


SET ENGINE_LINK=-luser32

REM del bin\*.pdb

SET INC_CORE_RT=-Ikohi.core\src -Ikohi.runtime\src
SET LNK_CORE_RT=-lkohi.core -lkohi.runtime

ECHO "%ACTION_STR% everything on %PLATFORM% (%TARGET%)..."

REM Version Generator - Build this first so it can be used later in the build process.
make -j -f "Makefile.kohi.mak" %ACTION% TARGET=%TARGET% ASSEMBLY=kohi.tools.versiongen BUILD_MODE=exe DO_VERSION=no FOLDER=
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

@REM ---------------------------------------------------
@REM Engine Libraries
@REM ---------------------------------------------------

REM Engine core lib
make -f "Makefile.kohi.mak" %ACTION% TARGET=%TARGET% ASSEMBLY=kohi.core BUILD_MODE=lib DO_VERSION=%DO_VERSION% ADDL_LINK_FLAGS="-lgdi32 -lwinmm %ENGINE_LINK%" FOLDER=
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

REM Engine runtime lib
make -f "Makefile.kohi.mak" %ACTION% TARGET=%TARGET% ASSEMBLY=kohi.runtime BUILD_MODE=lib DO_VERSION=%DO_VERSION% ADDL_INC_FLAGS="%INC_CORE_RT%" ADDL_LINK_FLAGS="-lkohi.core %ENGINE_LINK%" FOLDER=
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

REM Kohi Utils plugin lib
make -j -f "Makefile.kohi.mak" %ACTION% TARGET=%TARGET% ASSEMBLY=kohi.plugin.utils BUILD_MODE=lib DO_VERSION=%DO_VERSION% ADDL_INC_FLAGS="%INC_CORE_RT%" ADDL_LINK_FLAGS="%LNK_CORE_RT%" FOLDER=
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

REM Vulkan Renderer plugin lib
make -j -f "Makefile.kohi.mak" %ACTION% TARGET=%TARGET% ASSEMBLY=kohi.plugin.renderer.vulkan BUILD_MODE=lib DO_VERSION=%DO_VERSION% ADDL_INC_FLAGS="%INC_CORE_RT% -I%VULKAN_SDK%\include" ADDL_LINK_FLAGS="%LNK_CORE_RT% -lshaderc_shared -L%VULKAN_SDK%\Lib" FOLDER=
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

REM OpenAL plugin lib
make -j -f "Makefile.kohi.mak" %ACTION% TARGET=%TARGET% ASSEMBLY=kohi.plugin.audio.openal BUILD_MODE=lib DO_VERSION=%DO_VERSION% ADDL_INC_FLAGS="%INC_CORE_RT% -I'%programfiles(x86)%\OpenAL 1.1 SDK\include'" ADDL_LINK_FLAGS="%LNK_CORE_RT% -lopenal32 -L'%programfiles(x86)%\OpenAL 1.1 SDK\libs\win64'" FOLDER=
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

REM Kohi UI lib
make -j -f "Makefile.kohi.mak" %ACTION% TARGET=%TARGET% ASSEMBLY=kohi.plugin.ui.kui BUILD_MODE=lib DO_VERSION=%DO_VERSION% ADDL_INC_FLAGS="%INC_CORE_RT%" ADDL_LINK_FLAGS="%LNK_CORE_RT%" FOLDER=
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

@REM ---------------------------------------------------
@REM Engine Executables
@REM ---------------------------------------------------


REM Tools

REM Copy assimp lib/dll to bin folder.
@REM copy "%ASSIMP%\lib\x64\assimp-*.lib" "lib\assimp.lib"
setlocal enabledelayedexpansion
set "founddll="
for %%F in ("%ASSIMP%\bin\x64\assimp-*.dll") do (
    if not defined founddll (
        set "founddll=1"
        set "filename=%%~nxF"
        copy /Y "%%~F" "bin\!filename!"
    )
)
@REM copy "%ASSIMP%\bin\x64\assimp-*.dll" "bin\assimp.dll"
set "foundlib="
for %%F in ("%ASSIMP%\lib\x64\assimp-*.lib") do (
    if not defined foundlib (
        set "foundlib=1"
        copy /Y "%%~F" "lib\assimp.lib"
    )
)

set "ASSIMP_PATH=%ASSIMP:"=%"
make -j -f "Makefile.kohi.mak" %ACTION% TARGET=%TARGET% ASSEMBLY=kohi.tools BUILD_MODE=exe ADDL_INC_FLAGS="%INC_CORE_RT% -I'%ASSIMP_PATH%\include' " ADDL_LINK_FLAGS="%LNK_CORE_RT% -lassimp" FOLDER=
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

@REM ---------------------------------------------------
@REM Application Libraries and Executables
@REM ---------------------------------------------------

REM Testbed lib
make -j -f "Makefile.kohi.mak" %ACTION% TARGET=%TARGET% ASSEMBLY=testbed.klib BUILD_MODE=lib DO_VERSION=%DO_VERSION% ADDL_INC_FLAGS="%INC_CORE_RT% -Ikohi.plugin.ui.kui\src -Ikohi.plugin.audio.openal\src -Ikohi.plugin.utils\src" ADDL_LINK_FLAGS="%LNK_CORE_RT% -lkohi.plugin.ui.kui -lkohi.plugin.audio.openal -lkohi.plugin.utils" FOLDER=
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

REM Testbed
make -j -f "Makefile.kohi.mak" %ACTION% TARGET=%TARGET% ASSEMBLY=testbed.kapp BUILD_MODE=exe ADDL_INC_FLAGS="%INC_CORE_RT%" ADDL_LINK_FLAGS="%LNK_CORE_RT%" FOLDER=
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

REM Core Tests
make -j -f "Makefile.kohi.mak" %ACTION% TARGET=%TARGET% ASSEMBLY=kohi.core.tests BUILD_MODE=exe ADDL_INC_FLAGS="%INC_CORE_RT%" ADDL_LINK_FLAGS="%LNK_CORE_RT%" FOLDER=
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

REM Runtime Tests
make -j -f "Makefile.kohi.mak" %ACTION% TARGET=%TARGET% ASSEMBLY=kohi.runtime.tests BUILD_MODE=exe ADDL_INC_FLAGS="%INC_CORE_RT%" ADDL_LINK_FLAGS="%LNK_CORE_RT%" FOLDER=
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

ECHO All assemblies %ACTION_STR_PAST% successfully on %PLATFORM% (%TARGET%).
