@echo off
call %~dp0init_env.bat
rmdir /s /q build_clang_ninja_debug
cmake -B build_clang_ninja_debug -S src -G Ninja -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl -DCMAKE_BUILD_TYPE=Debug
