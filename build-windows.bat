@echo off
setlocal enabledelayedexpansion
REM Windows build script for the formuls JUCE app.
REM
REM Requirements:
REM   - Visual Studio 2022 with C++ desktop workload
REM   - JUCE at %USERPROFILE%\JUCE (or set PROJUCER to the Projucer.exe path)
REM   - MSYS2 with mingw-w64 toolchain (for building libpd)
REM   - faust (with faust2puredata)
REM   - Python 3  (python or python3 on PATH)
REM   - curl (ships with Windows 10+)
REM   - tar  (ships with Windows 10+)
REM   - 7z   (7-Zip, for unzipping the Open Stage Control package)
REM
REM Run from the repository root inside a Developer Command Prompt for VS 2022:
REM   build-windows.bat

set VERSION=0.3.1
set ROOT=%CD%

REM --- Locate Projucer ---
if not defined PROJUCER (
    if exist "%USERPROFILE%\JUCE\Projucer.exe" (
        set "PROJUCER=%USERPROFILE%\JUCE\Projucer.exe"
    )
)
if not defined PROJUCER (
    echo Projucer not found at %%USERPROFILE%%\JUCE\Projucer.exe
    echo Install JUCE at %%USERPROFILE%%\JUCE, or set PROJUCER to your Projucer.exe.
    exit /b 1
)
if not exist "%PROJUCER%" (
    echo Projucer not found at: %PROJUCER%
    exit /b 1
)

REM --- Locate Python ---
where python3 >nul 2>&1 && set "PYTHON=python3" || set "PYTHON=python"
%PYTHON% --version >nul 2>&1 || (
    echo Python 3 is required but not found on PATH.
    exit /b 1
)

REM --- Staging directories ---
if exist build rmdir /s /q build
mkdir build\gui
mkdir build\pd\externals
copy src\gui\_main.json build\gui\
copy src\gui\_formuls-default.state build\gui\
xcopy /s /e /i src\pd build\pd >nul

REM --- Faust Pd externals ---
cd "%ROOT%\src\faust"
call faust2puredata -vec -lv 0 -vs 4 f_repeater.dsp f_reverb.dsp formuls.dsp
if errorlevel 1 (
    echo faust2puredata failed
    exit /b 1
)
for %%f in (*.dll) do move "%%f" "..\..\build\pd\externals\" >nul

REM --- Ableton Link (abl_link~) Pd external ---
cd "%ROOT%\src\libs\abl_link\external"
make pdincludepath="%ROOT%/src/libs/libpd/pure-data/src"
if errorlevel 1 (
    echo abl_link~ build failed
    exit /b 1
)
for %%f in (*.dll) do move "%%f" "%ROOT%\build\pd\externals\" >nul

REM --- Download Open Stage Control ---
cd "%ROOT%\build"
curl -L -o open-stage-control_1.31.0_node.zip https://openstagecontrol.ammd.net/packages/open-stage-control_1.31.0_node.zip
if errorlevel 1 (
    echo Failed to download Open Stage Control
    exit /b 1
)
7z x open-stage-control_1.31.0_node.zip >nul
if errorlevel 1 (
    echo Failed to extract Open Stage Control (is 7z on PATH?)
    exit /b 1
)
move open-stage-control_1.31.0_node "%ROOT%\build\gui\open-stage-control" >nul
del open-stage-control_1.31.0_node.zip

REM --- Brand Open Stage Control ---
cd "%ROOT%"
sh src\tools\brand-osc.sh "%ROOT%\build\gui\open-stage-control" %VERSION%
if errorlevel 1 (
    echo brand-osc.sh failed
    exit /b 1
)

REM --- Patch Open Stage Control performance ---
%PYTHON% src\tools\patch-osc-perf.py "%ROOT%\build\gui\open-stage-control"
if errorlevel 1 (
    echo patch-osc-perf.py failed
    exit /b 1
)

REM --- Download Node.js for Windows x64 ---
cd "%ROOT%\build"
curl -L -o node-v22.17.0-win-x64.zip https://nodejs.org/dist/v22.17.0/node-v22.17.0-win-x64.zip
if errorlevel 1 (
    echo Failed to download Node.js
    exit /b 1
)
tar -xf node-v22.17.0-win-x64.zip
copy node-v22.17.0-win-x64\node.exe "%ROOT%\build\gui\node.exe"
rmdir /s /q node-v22.17.0-win-x64
del node-v22.17.0-win-x64.zip

REM --- Build libpd ---
cd "%ROOT%\src\libs\libpd"
mkdir libs 2>nul
cmake -B build-win -DCMAKE_BUILD_TYPE=Release -DPD_UTILS=ON -DPD_EXTRA=ON
if errorlevel 1 (
    echo libpd cmake configure failed
    exit /b 1
)
cmake --build build-win --config Release
if errorlevel 1 (
    echo libpd build failed
    exit /b 1
)
REM Copy the DLL and import lib to libs/ where the JUCE project expects them
copy build-win\Release\libpd.dll libs\ >nul 2>nul
copy build-win\Release\libpd.lib libs\ >nul 2>nul
if not exist libs\libpd.dll (
    REM Try alternative output paths
    for /r build-win %%f in (libpd.dll) do copy "%%f" libs\ >nul 2>nul
    for /r build-win %%f in (libpd.lib) do copy "%%f" libs\ >nul 2>nul
)
if not exist libs\libpd.dll (
    echo Could not find libpd.dll after build
    exit /b 1
)

REM --- Generate VS project and build the JUCE app ---
cd "%ROOT%"

REM Rewrite MODULEPATH to an absolute path, resave, then restore.
REM Walk up from Projucer.exe to find the JUCE root.
set "JUCE_DIR=%PROJUCER%"
:find_juce_root
for %%i in ("%JUCE_DIR%\..") do set "JUCE_DIR=%%~fi"
if exist "%JUCE_DIR%\modules\juce_core" goto found_juce
if "%JUCE_DIR%"=="%JUCE_DIR:\..=%" goto no_juce
goto find_juce_root

:no_juce
echo Could not find the JUCE modules folder above: %PROJUCER%
exit /b 1

:found_juce
set "JUCE_MODULES=%JUCE_DIR%\modules"

copy src\app\formuls.jucer build\formuls.jucer.orig >nul
%PYTHON% -c "import re,sys; t=open(sys.argv[1],'r').read(); t=re.sub(r'path=\"[^\"]*JUCE/modules\"','path=\"'+sys.argv[2].replace('\\','/')+'\"',t); open(sys.argv[1],'w').write(t)" src\app\formuls.jucer "%JUCE_MODULES%"
"%PROJUCER%" --resave src\app\formuls.jucer
copy /y build\formuls.jucer.orig src\app\formuls.jucer >nul
del build\formuls.jucer.orig

REM Build with MSBuild
msbuild src\app\Builds\VisualStudio2022\formuls.sln /p:Configuration=Release /p:Platform=x64 /m
if errorlevel 1 (
    echo JUCE app build failed
    exit /b 1
)

REM --- Assemble distributable folder ---
set "OUT=formuls-%VERSION%-windows-x64"
if exist "%OUT%" rmdir /s /q "%OUT%"
mkdir "%OUT%"
copy src\app\Builds\VisualStudio2022\x64\Release\formuls.exe "%OUT%\"
copy src\libs\libpd\libs\libpd.dll "%OUT%\"
xcopy /s /e /i build\pd "%OUT%\pd" >nul
xcopy /s /e /i build\gui "%OUT%\gui" >nul

REM --- Clean up ---
rmdir /s /q build
cd "%ROOT%\src\libs\libpd"
rmdir /s /q build-win 2>nul
del libs\libpd.dll libs\libpd.lib 2>nul

echo Done: %ROOT%\%OUT%
exit /b 0
