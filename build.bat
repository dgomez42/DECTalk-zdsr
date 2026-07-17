@echo off
set SRC=%~dp0
cd /d "%SRC%"

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x86
if errorlevel 1 (
    echo Failed to init VS2022 environment
    exit /b 1
)

echo Compiling resources...
rc /nologo DECTalk.rc

echo Building DECTalk.dll...
cl /nologo /O2 /MT /W3 /GS- /D WIN32 /D NDEBUG /D DECTALK_EXPORTS /D _WINDOWS /D _USRDLL /LD DECTalk.cpp wave_player.cpp /link /DEF:DECTalk.def /SUBSYSTEM:WINDOWS winmm.lib user32.lib kernel32.lib gdi32.lib DECTalk.res /OUT:DECTalk.dll

if errorlevel 1 (
    echo Build failed
    exit /b 1
)

echo.
echo Done: DECTalk.dll
echo.
echo Files in output:
dir DECTalk.dll
