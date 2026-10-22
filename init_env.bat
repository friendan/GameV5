@echo off
set "VCFIRST=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat"
set "VCSECOND=C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat"
if exist "%VCFIRST%" (
    call "%VCFIRST%" x64 2>nul
    goto :eof
)
if exist "%VCSECOND%" (
    call "%VCSECOND%" x64 2>nul
    goto :eof
)
echo No vcvarsall.bat found & exit /b 1
