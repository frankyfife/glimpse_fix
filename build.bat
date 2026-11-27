@echo off
rem Builds dinput8.dll (x86) with the VS 2022 Build Tools
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul || exit /b 1
cd /d "%~dp0"
if not exist build mkdir build
rem the SMAA passes (src\smaa.hlsl around third_party\smaa\SMAA.hlsl) as Direct3D 9 bytecode headers
for %%s in (EdgeVS:vs_3_0 EdgePS:ps_3_0 WeightVS:vs_3_0 WeightPS:ps_3_0 BlendVS:vs_3_0 BlendPS:ps_3_0) do (
  for /f "tokens=1,2 delims=:" %%a in ("%%s") do (
    fxc /nologo /O3 /T %%b /E %%a /Vn g_smaa%%a /Fh build\smaa_%%a.h src\smaa.hlsl >nul || exit /b 1
  )
)
rem the pickup boxes of [DLC] Pickups (src\pickups.hlsl)
fxc /nologo /O3 /T vs_3_0 /E PickupVS /Vn g_pickupVS /Fh build\pickup_VS.h src\pickups.hlsl >nul || exit /b 1
fxc /nologo /O3 /T ps_3_0 /E PickupPS /Vn g_pickupPS /Fh build\pickup_PS.h src\pickups.hlsl >nul || exit /b 1
fxc /nologo /O3 /T vs_3_0 /E PickupModelVS /Vn g_pickupModelVS /Fh build\pickup_ModelVS.h src\pickups.hlsl >nul || exit /b 1
fxc /nologo /O3 /T ps_3_0 /E PickupModelPS /Vn g_pickupModelPS /Fh build\pickup_ModelPS.h src\pickups.hlsl >nul || exit /b 1
fxc /nologo /O3 /T ps_3_0 /E PickupGainPS /Vn g_pickupGainPS /Fh build\pickup_GainPS.h src\pickups.hlsl >nul || exit /b 1
cl /nologo /O2 /MT /W4 /EHsc /GS- /D_CRT_SECURE_NO_WARNINGS /Ibuild /Fobuild\ /c src\main.cpp src\dinput_wrap.cpp src\game.cpp src\diag.cpp src\framerate.cpp src\audio_diag.cpp src\postfx.cpp src\savediag.cpp src\boost.cpp src\camera.cpp src\envlog.cpp src\builds.cpp src\online.cpp src\audio_fix.cpp src\hdr.cpp src\ultrawide.cpp src\texdump.cpp src\overlay.cpp src\smaa.cpp src\dlcfs.cpp src\dlcmode.cpp src\dlccars.cpp src\vark.cpp src\iohook.cpp src\glass.cpp src\dlcpickups.cpp src\dlcmusic.cpp src\dlcrivals.cpp src\dlcrecords.cpp src\ui_hook.cpp src\lap_menu.cpp || exit /b 1
link /nologo /DLL /OUT:build\dinput8.dll /DEF:src\dinput8.def build\main.obj build\dinput_wrap.obj build\game.obj build\diag.obj build\framerate.obj build\audio_diag.obj build\postfx.obj build\savediag.obj build\boost.obj build\camera.obj build\envlog.obj build\builds.obj build\online.obj build\audio_fix.obj build\hdr.obj build\ultrawide.obj build\texdump.obj build\overlay.obj build\smaa.obj build\dlcfs.obj build\dlcmode.obj build\dlccars.obj build\vark.obj build\iohook.obj build\glass.obj build\dlcpickups.obj build\dlcmusic.obj build\dlcrivals.obj build\dlcrecords.obj build\ui_hook.obj build\lap_menu.obj dxguid.lib user32.lib gdi32.lib shell32.lib kernel32.lib advapi32.lib || exit /b 1
cl /nologo /O2 /MT /W4 /D_CRT_SECURE_NO_WARNINGS /Fobuild\ /Febuild\setlaa.exe tools\setlaa.c || exit /b 1
rc /nologo /fo build\glimpse_config.res config\glimpse_config.rc || exit /b 1
cl /nologo /O2 /MT /W4 /EHsc /D_CRT_SECURE_NO_WARNINGS /Fobuild\ /Febuild\glimpse_config.exe config\glimpse_config.cpp build\glimpse_config.res /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib comctl32.lib comdlg32.lib || exit /b 1
echo built build\dinput8.dll build\setlaa.exe build\glimpse_config.exe
