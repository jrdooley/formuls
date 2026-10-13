@echo off
setlocal enabledelayedexpansion
REM Windows build script for the formuls JUCE app.
REM
REM Requirements:
REM   - Visual Studio 2022 with C++ desktop workload (cl, msbuild, cmake)
REM   - JUCE at %USERPROFILE%\JUCE (or set PROJUCER to the Projucer.exe path)
REM   - a standalone vcpkg (C:\vcpkg, or set FORMULS_VCPKG), for the pthreads libpd needs
REM   - faust on PATH (faust2puredata is not needed)
REM   - sh on PATH (Git for Windows), for src\tools\brand-osc.sh
REM   - Python 3  (python or python3 on PATH)
REM   - curl (ships with Windows 10+)
REM   - tar  (ships with Windows 10+)
REM   - 7z   (7-Zip, for unzipping the Open Stage Control package)
REM
REM Run from the repository root inside a Developer Command Prompt for VS 2022:
REM   build-windows.bat

set VERSION=0.3.1
REM Python's file I/O defaults to the Windows code page (cp1252), which cannot
REM hold characters in Open Stage Control's sources and would misread UTF-8.
REM UTF-8 mode makes the build tools behave as on macOS and Linux.
set PYTHONUTF8=1
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
copy src\gui\_formuls-default.state build\gui\
xcopy /s /e /i src\pd build\pd >nul

REM --- Build libpd ---
REM Built first: the Pd externals below link against it. With MSVC, libpd needs
REM a pthreads library; vcpkg provides one (it is installed on GitHub's
REM Windows machines; elsewhere, install vcpkg and set it). It must be a
REM standalone vcpkg: the copy inside Visual Studio, which a Developer Command
REM Prompt puts in VCPKG_ROOT, only installs for projects with a vcpkg.json.
REM GitHub's machines name the standalone one in VCPKG_INSTALLATION_ROOT.
set "VCPKG=C:\vcpkg"
if defined VCPKG_INSTALLATION_ROOT set "VCPKG=%VCPKG_INSTALLATION_ROOT%"
if defined FORMULS_VCPKG set "VCPKG=%FORMULS_VCPKG%"
"%VCPKG%\vcpkg" install pthreads:x64-windows
if errorlevel 1 (
    echo vcpkg could not install pthreads from %VCPKG%
    exit /b 1
)
set "PTHREADS=%VCPKG%\installed\x64-windows"
cd "%ROOT%\src\libs\libpd"
mkdir libs 2>nul
cmake -B build-win -A x64 -DPD_UTILS=ON -DPD_EXTRA=ON -DPTHREADS_LIB="%PTHREADS%\lib\pthreadVC3.lib" -DPTHREADS_INCLUDE_DIR="%PTHREADS%\include"
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
for /r build-win %%f in (libpd.dll libpd.lib) do if exist "%%f" copy /y "%%f" libs\ >nul
if not exist libs\libpd.dll (
    echo Could not find libpd.dll after build
    exit /b 1
)
copy /y "%PTHREADS%\bin\pthreadVC3.dll" libs\ >nul
set "PDSRC=%ROOT%\src\libs\libpd\pure-data\src"
set "PDLIB=%ROOT%\src\libs\libpd\libs\libpd.lib"

REM --- Faust Pd externals ---
REM faust2puredata is a Unix shell script, so do what it does: Faust writes
REM C++ from the puredata.cpp architecture, and cl builds each into a DLL
REM linked against libpd (the app's Pd, not pd.dll). Pd finds an external's
REM setup function by name, so it is exported explicitly.
cd "%ROOT%\src\faust"
for %%d in (f_repeater f_reverb formuls) do (
    faust -i -a puredata.cpp -vec -lv 0 -vs 4 %%d.dsp -o "%ROOT%\build\%%d.cpp"
    if errorlevel 1 (
        echo faust failed on %%d.dsp
        exit /b 1
    )
    cl /nologo /O2 /EHsc /LD /DNEWIO=0 /Dmydsp=%%d /I"%PDSRC%" "%ROOT%\build\%%d.cpp" /Fo"%ROOT%\build\\" /link "%PDLIB%" /EXPORT:%%d_tilde_setup /OUT:"%ROOT%\build\pd\externals\%%d~.dll"
    if errorlevel 1 (
        echo compiling %%d~ failed
        exit /b 1
    )
)

REM --- Ableton Link (abl_link~) Pd external ---
REM Its makefile (pd-lib-builder) expects MinGW; build it with cl like the rest.
cd "%ROOT%\src\libs\abl_link\external"
cl /nologo /O2 /EHsc /LD /std:c++14 /DLINK_PLATFORM_WINDOWS=1 /D_WIN32_WINNT=0x0601 /I"link\include" /I"link\modules\asio-standalone\asio\include" /I"%PDSRC%" abl_link~.cpp abl_link_instance.cpp /Fo"%ROOT%\build\\" /link "%PDLIB%" ws2_32.lib iphlpapi.lib winmm.lib /EXPORT:abl_link_tilde_setup /OUT:"%ROOT%\build\pd\externals\abl_link~.dll"
if errorlevel 1 (
    echo abl_link~ build failed
    exit /b 1
)

REM --- Download Open Stage Control ---
cd "%ROOT%\build"
curl -L -o open-stage-control_1.31.0_node.zip https://openstagecontrol.ammd.net/packages/open-stage-control_1.31.0_node.zip
if errorlevel 1 (
    echo Failed to download Open Stage Control
    exit /b 1
)
7z x open-stage-control_1.31.0_node.zip >nul
if errorlevel 1 (
    echo Failed to extract Open Stage Control ^(is 7z on PATH?^)
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

REM --- Flatten the session and compile compounds (as build-macOS.sh) ---
REM src\gui\_main.json stays the clone-based file you edit; the build ships it
REM flattened, with slider compounds and sequencer panels compiled to canvases.
REM See docs/gui/session-size.md.
%PYTHON% src\tools\flatten-session.py "%ROOT%\src\gui\_main.json" "%ROOT%\build\gui\_main.json" --defaults "%ROOT%\src\tools\osc-defaults-1.31.0.json" --osc-package "%ROOT%\build\gui\open-stage-control" --template-tab formuls0 --template-tab Widgets
if errorlevel 1 (
    echo flatten-session.py failed
    exit /b 1
)
%PYTHON% src\tools\compile-compounds.py "%ROOT%\build\gui\_main.json" "%ROOT%\src\gui\_formuls-default.state" "%ROOT%\build\gui\_main.json" "%ROOT%\build\gui\_formuls-default.state" "%ROOT%\build\gui\compounds.json" --lib "%ROOT%\src\gui\compounds\slider-lib.js" --seq-lib "%ROOT%\src\gui\compounds\sequencer-lib.js"
if errorlevel 1 (
    echo compile-compounds.py failed
    exit /b 1
)
copy src\gui\formuls-module.js build\gui\ >nul

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
%PYTHON% src\tools\jucer-module-path.py src\app\formuls.jucer "%JUCE_MODULES%"
if errorlevel 1 (
    copy /y build\formuls.jucer.orig src\app\formuls.jucer >nul
    exit /b 1
)
"%PROJUCER%" --resave src\app\formuls.jucer
set RESAVE_ERROR=%ERRORLEVEL%
copy /y build\formuls.jucer.orig src\app\formuls.jucer >nul
del build\formuls.jucer.orig
if not exist src\app\Builds\VisualStudio2022\formuls.sln (
    echo Projucer did not write the Visual Studio project ^(exit code %RESAVE_ERROR%^)
    exit /b 1
)

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
REM JUCE puts the app target in an App subfolder.
copy src\app\Builds\VisualStudio2022\x64\Release\App\formuls.exe "%OUT%\"
copy src\libs\libpd\libs\libpd.dll "%OUT%\"
copy src\libs\libpd\libs\pthreadVC3.dll "%OUT%\"
xcopy /s /e /i build\pd "%OUT%\pd" >nul
xcopy /s /e /i build\gui "%OUT%\gui" >nul
REM A failed copy only prints a message, so check the package is complete.
for %%f in (formuls.exe libpd.dll pthreadVC3.dll gui\node.exe gui\_main.json gui\compounds.json gui\formuls-module.js pd\_main.pd pd\externals\formuls~.dll pd\externals\f_reverb~.dll pd\externals\f_repeater~.dll pd\externals\abl_link~.dll) do (
    if not exist "%OUT%\%%f" (
        echo Missing from the package: %%f
        exit /b 1
    )
)

REM --- Clean up ---
rmdir /s /q build
cd "%ROOT%\src\libs\libpd"
rmdir /s /q build-win 2>nul
del libs\libpd.dll libs\libpd.lib 2>nul

echo Done: %ROOT%\%OUT%
exit /b 0
