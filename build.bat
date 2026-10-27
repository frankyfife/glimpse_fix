@echo off
rem Builds dinput8.dll (x86) with the VS 2022 Build Tools
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul || exit /b 1
cd /d "%~dp0"
if not exist build mkdir build
cl /nologo /O2 /MT /W4 /EHsc /GS- /D_CRT_SECURE_NO_WARNINGS /Fobuild\ /c src\main.cpp src\dinput_wrap.cpp src\game.cpp src\diag.cpp src\profile.cpp src\framerate.cpp src\audio_diag.cpp src\postfx.cpp || exit /b 1
link /nologo /DLL /OUT:build\dinput8.dll /DEF:src\dinput8.def build\main.obj build\dinput_wrap.obj build\game.obj build\diag.obj build\profile.obj build\framerate.obj build\audio_diag.obj build\postfx.obj dxguid.lib user32.lib kernel32.lib || exit /b 1
cl /nologo /O2 /MT /W4 /D_CRT_SECURE_NO_WARNINGS /Fobuild\ /Febuild\setlaa.exe tools\setlaa.c || exit /b 1
echo built build\dinput8.dll build\setlaa.exe
