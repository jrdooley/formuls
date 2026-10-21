@echo off
setlocal enabledelayedexpansion
REM Builds the formuls instrument plugin (VST3) for Windows.
REM
REM The plugin runs the same Pd patch and serves the same control GUI as the
REM app, so it takes those resources (pd\ with its compiled .dll externals, and
REM the flattened gui\) from a Windows app build made by build-windows.bat,
REM rather than compiling Faust and fetching Open Stage Control a second time.
REM node.exe is not copied: the plugin serves the GUI itself.
REM
REM Requirements: as build-windows.bat (Visual Studio 2022, JUCE, vcpkg, Python 3).
REM Run from the repository root inside a Developer Command Prompt for VS 2022:
REM   build-plugin-windows.bat
REM   set FORMULS_APP=C:\path\to\formuls-windows-folder  (default formuls-VERSION-windows-x64)
REM   set INSTALL=1   also copies it into %LOCALAPPDATA%\Programs\Common\VST3
REM
REM Produces formuls-VERSION-windows-x64-plugins\formuls.vst3:
REM   Contents\x86_64-win\{formuls.vst3, libpd.dll, pthreadVC3.dll}
REM   Contents\Resources\{pd, gui, moduleinfo.json}
REM The plugin delay-loads libpd.dll and loads this copy by full path
REM (SharedEngine.cpp); ResourceLocator finds Contents\Resources.
REM See docs/plugin/README.md.

set VERSION=0.3.2
set PYTHONUTF8=1
set "ROOT=%CD%"

if not defined FORMULS_APP set "FORMULS_APP=%ROOT%\formuls-%VERSION%-windows-x64"
for %%f in (pd\_main.pd gui\_main.json gui\compounds.json pd\externals\formuls~.dll pd\externals\abl_link~.dll) do (
    if not exist "%FORMULS_APP%\%%f" (
        echo No built app resources at %FORMULS_APP% ^(missing %%f^).
        echo Run build-windows.bat first, or set FORMULS_APP to a built formuls Windows folder.
        exit /b 1
    )
)

REM --- Locate Projucer and Python (as build-windows.bat) ---
if not defined PROJUCER (
    if exist "%USERPROFILE%\JUCE\Projucer.exe" set "PROJUCER=%USERPROFILE%\JUCE\Projucer.exe"
)
if not defined PROJUCER (
    echo Projucer not found at %%USERPROFILE%%\JUCE\Projucer.exe; set PROJUCER.
    exit /b 1
)
if not exist "%PROJUCER%" (
    echo Projucer not found at: %PROJUCER%
    exit /b 1
)
where python3 >nul 2>&1 && set "PYTHON=python3" || set "PYTHON=python"
%PYTHON% --version >nul 2>&1 || (
    echo Python 3 is required but not found on PATH.
    exit /b 1
)

REM --- libpd ---
REM The plugin links libpd.lib. build-windows.bat deletes it when it finishes,
REM so build it again the same way if it is missing.
set "LIBPD=%ROOT%\src\libs\libpd\libs"
if exist "%LIBPD%\libpd.lib" if exist "%LIBPD%\libpd.dll" if exist "%LIBPD%\pthreadVC3.dll" goto have_libpd
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
for /r build-win %%f in (libpd.dll libpd.lib) do if exist "%%f" copy /y "%%f" libs\ >nul
copy /y "%PTHREADS%\bin\pthreadVC3.dll" libs\ >nul
cd "%ROOT%"
if not exist "%LIBPD%\libpd.lib" (
    echo Could not find libpd.lib after the build
    exit /b 1
)
:have_libpd

REM --- Generate the Visual Studio project ---
REM Point the module paths at wherever JUCE is, resave, restore the .jucer.
set "JUCE_DIR=%PROJUCER%"
:find_juce_root
for %%i in ("%JUCE_DIR%\..") do set "JUCE_DIR=%%~fi"
if exist "%JUCE_DIR%\modules\juce_core" goto found_juce
if "%JUCE_DIR:~-1%"=="\" goto no_juce
goto find_juce_root
:no_juce
echo Could not find the JUCE modules folder above: %PROJUCER%
exit /b 1
:found_juce

set "JUCER=%ROOT%\src\plugin\formuls-plugin.jucer"
copy /y "%JUCER%" "%JUCER%.orig" >nul
%PYTHON% src\tools\jucer-module-path.py "%JUCER%" "%JUCE_DIR%\modules"
if errorlevel 1 (
    copy /y "%JUCER%.orig" "%JUCER%" >nul
    del "%JUCER%.orig"
    exit /b 1
)
"%PROJUCER%" --resave "%JUCER%"
set RESAVE_ERROR=%ERRORLEVEL%
copy /y "%JUCER%.orig" "%JUCER%" >nul
del "%JUCER%.orig"
set "SLN=%ROOT%\src\plugin\Builds\VisualStudio2022\formuls.sln"
if not exist "%SLN%" (
    echo Projucer did not write the Visual Studio project ^(exit code %RESAVE_ERROR%^)
    exit /b 1
)

REM --- Build the VST3 target ---
REM Only VST3: the .jucer also lists AU (macOS) and LV2 (Linux). The VST3
REM manifest helper loads the plugin to write moduleinfo.json; libpd.dll is on
REM PATH for it in case it needs to.
set "PATH=%LIBPD%;%PATH%"
REM Build the three projects one at a time, in dependency order. Through the
REM solution (/t:formuls_-_VST3) msbuild hands that target name on to every
REM dependency, which fails with MSB4057. The dependencies are only declared in
REM the .sln, so set SolutionDir for the output paths the projects share. The
REM VST3 manifest helper is Win32-only in JUCE's exporter.
set "VSDIR=%ROOT%\src\plugin\Builds\VisualStudio2022"
set MSB=/p:Configuration=Release "/p:SolutionDir=%VSDIR%\\" /m
msbuild "%VSDIR%\formuls_SharedCode.vcxproj" /p:Platform=x64 %MSB%
if errorlevel 1 (
    echo plugin build failed ^(Shared Code^)
    exit /b 1
)
msbuild "%VSDIR%\formuls_VST3ManifestHelper.vcxproj" /p:Platform=Win32 %MSB%
if errorlevel 1 (
    echo plugin build failed ^(VST3 manifest helper^)
    exit /b 1
)
msbuild "%VSDIR%\formuls_VST3.vcxproj" /p:Platform=x64 %MSB%
if errorlevel 1 (
    echo plugin build failed
    exit /b 1
)

REM --- Assemble the bundle ---
set "BUILT=%ROOT%\src\plugin\Builds\VisualStudio2022\x64\Release\VST3\formuls.vst3"
if not exist "%BUILT%\Contents\x86_64-win\formuls.vst3" (
    echo No built bundle at %BUILT%
    exit /b 1
)
set "DIST=%ROOT%\formuls-%VERSION%-windows-x64-plugins"
set "VST3=%DIST%\formuls.vst3"
if exist "%DIST%" rmdir /s /q "%DIST%"
mkdir "%DIST%"
xcopy /s /e /i /q "%BUILT%" "%VST3%" >nul
copy /y "%LIBPD%\libpd.dll" "%VST3%\Contents\x86_64-win\" >nul
copy /y "%LIBPD%\pthreadVC3.dll" "%VST3%\Contents\x86_64-win\" >nul
xcopy /s /e /i /q "%FORMULS_APP%\pd" "%VST3%\Contents\Resources\pd" >nul
xcopy /s /e /i /q "%FORMULS_APP%\gui" "%VST3%\Contents\Resources\gui" >nul
if exist "%VST3%\Contents\Resources\gui\node.exe" del "%VST3%\Contents\Resources\gui\node.exe"

REM A failed copy only prints a message, so check the bundle is complete.
for %%f in (x86_64-win\formuls.vst3 x86_64-win\libpd.dll x86_64-win\pthreadVC3.dll Resources\pd\_main.pd Resources\pd\externals\formuls~.dll Resources\pd\externals\f_reverb~.dll Resources\pd\externals\f_repeater~.dll Resources\pd\externals\abl_link~.dll Resources\gui\_main.json Resources\gui\compounds.json Resources\gui\formuls-module.js) do (
    if not exist "%VST3%\Contents\%%f" (
        echo Missing from the bundle: %%f
        exit /b 1
    )
)
echo Built %VST3%

if "%INSTALL%"=="1" (
    set "DEST=%LOCALAPPDATA%\Programs\Common\VST3"
    if not exist "!DEST!" mkdir "!DEST!"
    if exist "!DEST!\formuls.vst3" rmdir /s /q "!DEST!\formuls.vst3"
    xcopy /s /e /i /q "%VST3%" "!DEST!\formuls.vst3" >nul
    echo Installed into !DEST!
)
exit /b 0
