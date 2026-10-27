@echo off
rem Builds voice_dialogue.dll (Release, Win32) without opening Visual Studio.
cd /d "%~dp0"
set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -prerelease -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do set MSBUILD=%%i
if not defined MSBUILD (
  echo Could not find MSBuild. Is Visual Studio installed?
  pause
  exit /b 1
)
"%MSBUILD%" voice_dialogue.sln /t:Rebuild /p:Configuration=Release /p:Platform=Win32 /v:minimal
echo.
if errorlevel 1 (echo BUILD FAILED, see the errors above.) else (echo Build OK: Release\voice_dialogue.dll)
pause
