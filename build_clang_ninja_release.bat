@echo off
call %~dp0init_env.bat
cmake --build build_clang_ninja_release --config Release
