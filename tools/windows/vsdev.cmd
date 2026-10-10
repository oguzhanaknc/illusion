@echo off
rem Runs a command inside a Visual Studio x64 developer environment (cl, link, ninja on PATH).
rem Usage: tools\windows\vsdev.cmd cmake --preset windows-msvc
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo vswhere.exe not found. Install Visual Studio 2022 or newer with the C++ workload. 1>&2
    exit /b 1
)
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
if not defined VSINSTALL (
    echo No Visual Studio installation with the C++ toolset was found. 1>&2
    exit /b 1
)
rem vcvars itself looks for vswhere on PATH.
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
%*
exit /b %ERRORLEVEL%
