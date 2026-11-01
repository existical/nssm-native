@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -no_logo -arch=x64 -host_arch=x64
if errorlevel 1 exit /b 1
if not exist "%~dp0x64" mkdir "%~dp0x64"
pushd "%~dp0x64"
call "..\..\version.cmd"
if not exist version.h goto failed
mc -u -U "..\..\messages.mc" -r . -h .
if errorlevel 1 goto failed
rc /nologo /d NDEBUG /d _WIN64 /d UNICODE /d _UNICODE /l 0x809 /i . /i "..\.." /fo nssm.res "..\..\nssm.rc"
if errorlevel 1 goto failed
cl /nologo /MP /O2 /Ob1 /GF /MT /Gy /W3 /Zi /EHsc /DWIN32 /DNDEBUG /D_CONSOLE /DUNICODE /D_UNICODE /I . /Fdcompiler.pdb /Fe"..\..\nssm.exe" "..\..\account.cpp" "..\..\console.cpp" "..\..\env.cpp" "..\..\event.cpp" "..\..\gui.cpp" "..\..\hook.cpp" "..\..\imports.cpp" "..\..\io.cpp" "..\..\nssm.cpp" "..\..\process.cpp" "..\..\registry.cpp" "..\..\service.cpp" "..\..\settings.cpp" "..\..\utf8.cpp" nssm.res /link /INCREMENTAL:NO /OPT:REF /OPT:ICF /DYNAMICBASE /NXCOMPAT /SUBSYSTEM:CONSOLE /MACHINE:X64 /PDB:nssm.pdb kernel32.lib user32.lib gdi32.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib psapi.lib shlwapi.lib comctl32.lib
if errorlevel 1 goto failed
popd
exit /b 0
:failed
popd
exit /b 1
