cmake_minimum_required(VERSION 3.10)

if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(GameV5 VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_definitions(-DCMAKE)

if(MSVC OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    add_compile_options(/W3 /WX- /utf-8)
endif()

foreach(_flag_var
    CMAKE_C_FLAGS_DEBUG CMAKE_CXX_FLAGS_DEBUG
    CMAKE_C_FLAGS_RELEASE CMAKE_CXX_FLAGS_RELEASE
    CMAKE_C_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_MINSIZEREL
    CMAKE_C_FLAGS_RELWITHDEBINFO CMAKE_CXX_FLAGS_RELWITHDEBINFO)
    if(DEFINED ${_flag_var})
        string(REGEX REPLACE "/MD" "/MT" ${_flag_var} "${${_flag_var}}")
    endif()
endforeach()

set(CMAKE_DEBUG_POSTFIX "_mtd")
set(CMAKE_RELEASE_POSTFIX "_mt")
set(CMAKE_MINSIZEREL_POSTFIX "_mt")
set(CMAKE_RELWITHDEBINFO_POSTFIX "_mt")

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/../bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/../bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/../bin)

set(GAMEV5_SRC_DIR "${CMAKE_SOURCE_DIR}/GameV5")
set(GAMEV5_SKIN_DIR "${CMAKE_SOURCE_DIR}/../bin/skin/GameV5")
set(GAMEV5_SKIN_ZIP "${CMAKE_CURRENT_BINARY_DIR}/GameV5Skin.zip")
file(TO_CMAKE_PATH "${GAMEV5_SKIN_ZIP}" GAMEV5_SKIN_ZIP_RC_PATH)
configure_file(
    ${GAMEV5_SRC_DIR}/GameV5Zip.rc.in
    ${CMAKE_CURRENT_BINARY_DIR}/GameV5Zip.rc
    @ONLY
)

# ---------- DUIX（DuiLib Ultimate）----------
if(DEFINED ENV{DUIX} AND NOT "$ENV{DUIX}" STREQUAL "")
    set(DUIX_ROOT "$ENV{DUIX}")
else()
    set(DUIX_ROOT "H:/sdk/DuiLib/trunk/3rd/DuiLib_Ultimate-master")
endif()
get_filename_component(DUIX_ROOT "${DUIX_ROOT}" ABSOLUTE)
if(NOT EXISTS "${DUIX_ROOT}/src/DuiLib/UIlib.h")
    message(FATAL_ERROR "DUIX not found (set env DUIX). Tried: ${DUIX_ROOT}")
endif()
message(STATUS "DUIX_ROOT=${DUIX_ROOT}")

set(DUIX_INCLUDE "${DUIX_ROOT}/src/DuiLib")
set(DUIX_BIN "${DUIX_ROOT}/bin")
set(DUIX_WEBVIEW2_ROOT "${DUIX_ROOT}/src/3rd/webview2/pkg/build/native")
set(WEBVIEW2_ROOT "${DUIX_WEBVIEW2_ROOT}")
message(STATUS "WEBVIEW2_ROOT=${WEBVIEW2_ROOT}")

add_library(DuiLib STATIC IMPORTED GLOBAL)
set_target_properties(DuiLib PROPERTIES
    IMPORTED_LOCATION_DEBUG "${DUIX_BIN}/DuiLib_mtd.lib"
    IMPORTED_LOCATION_RELEASE "${DUIX_BIN}/DuiLib_mt.lib"
    IMPORTED_LOCATION_MINSIZEREL "${DUIX_BIN}/DuiLib_mt.lib"
    IMPORTED_LOCATION_RELWITHDEBINFO "${DUIX_BIN}/DuiLib_mt.lib"
    IMPORTED_LOCATION "${DUIX_BIN}/DuiLib_mtd.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${DUIX_INCLUDE};${WEBVIEW2_ROOT}/include"
    INTERFACE_COMPILE_DEFINITIONS "UILIB_STATIC;DUILIB_RENDER_BACKEND=1;DUILIB_HAS_WEBVIEW2=1"
)

add_library(lunasvg STATIC IMPORTED GLOBAL)
set_target_properties(lunasvg PROPERTIES
    IMPORTED_LOCATION_DEBUG "${DUIX_BIN}/lunasvg_mtd.lib"
    IMPORTED_LOCATION_RELEASE "${DUIX_BIN}/lunasvg_mt.lib"
    IMPORTED_LOCATION_MINSIZEREL "${DUIX_BIN}/lunasvg_mt.lib"
    IMPORTED_LOCATION_RELWITHDEBINFO "${DUIX_BIN}/lunasvg_mt.lib"
    IMPORTED_LOCATION "${DUIX_BIN}/lunasvg_mtd.lib"
)

add_library(plutovg STATIC IMPORTED GLOBAL)
set_target_properties(plutovg PROPERTIES
    IMPORTED_LOCATION_DEBUG "${DUIX_BIN}/plutovg_mtd.lib"
    IMPORTED_LOCATION_RELEASE "${DUIX_BIN}/plutovg_mt.lib"
    IMPORTED_LOCATION_MINSIZEREL "${DUIX_BIN}/plutovg_mt.lib"
    IMPORTED_LOCATION_RELWITHDEBINFO "${DUIX_BIN}/plutovg_mt.lib"
    IMPORTED_LOCATION "${DUIX_BIN}/plutovg_mtd.lib"
)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(_WV2_ARCH x64)
else()
    set(_WV2_ARCH x86)
endif()
set(_WV2_LOADER "${WEBVIEW2_ROOT}/${_WV2_ARCH}/WebView2LoaderStatic.lib")
if(NOT EXISTS "${_WV2_LOADER}")
    message(FATAL_ERROR "WebView2LoaderStatic.lib not found: ${_WV2_LOADER}")
endif()

# ---------- Lua 5.5（静态库，供后续嵌入；不含 lua.exe / luac）----------
set(LUA_SRC_DIR "${CMAKE_SOURCE_DIR}/3rd/lua-5.5.0/src")
set(LUA55_SOURCES
    ${LUA_SRC_DIR}/lapi.c
    ${LUA_SRC_DIR}/lauxlib.c
    ${LUA_SRC_DIR}/lbaselib.c
    ${LUA_SRC_DIR}/lcode.c
    ${LUA_SRC_DIR}/lcorolib.c
    ${LUA_SRC_DIR}/lctype.c
    ${LUA_SRC_DIR}/ldblib.c
    ${LUA_SRC_DIR}/ldebug.c
    ${LUA_SRC_DIR}/ldo.c
    ${LUA_SRC_DIR}/ldump.c
    ${LUA_SRC_DIR}/lfunc.c
    ${LUA_SRC_DIR}/lgc.c
    ${LUA_SRC_DIR}/linit.c
    ${LUA_SRC_DIR}/liolib.c
    ${LUA_SRC_DIR}/llex.c
    ${LUA_SRC_DIR}/lmathlib.c
    ${LUA_SRC_DIR}/lmem.c
    ${LUA_SRC_DIR}/loadlib.c
    ${LUA_SRC_DIR}/lobject.c
    ${LUA_SRC_DIR}/lopcodes.c
    ${LUA_SRC_DIR}/loslib.c
    ${LUA_SRC_DIR}/lparser.c
    ${LUA_SRC_DIR}/lstate.c
    ${LUA_SRC_DIR}/lstring.c
    ${LUA_SRC_DIR}/lstrlib.c
    ${LUA_SRC_DIR}/ltable.c
    ${LUA_SRC_DIR}/ltablib.c
    ${LUA_SRC_DIR}/ltm.c
    ${LUA_SRC_DIR}/lundump.c
    ${LUA_SRC_DIR}/lutf8lib.c
    ${LUA_SRC_DIR}/lvm.c
    ${LUA_SRC_DIR}/lzio.c
)
add_library(lua55 STATIC ${LUA55_SOURCES})
target_include_directories(lua55 PUBLIC "${LUA_SRC_DIR}")
set_target_properties(lua55 PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    DEBUG_POSTFIX "${CMAKE_DEBUG_POSTFIX}"
    RELEASE_POSTFIX "${CMAKE_RELEASE_POSTFIX}"
    MINSIZEREL_POSTFIX "${CMAKE_MINSIZEREL_POSTFIX}"
    RELWITHDEBINFO_POSTFIX "${CMAKE_RELWITHDEBINFO_POSTFIX}"
)

# ---------- libhv 1.3.4（静态库；关示例/动态库，沿用上游 hv_static）----------
set(BUILD_SHARED OFF)
set(BUILD_STATIC ON)
set(BUILD_EXAMPLES OFF)
set(BUILD_UNITTEST OFF)
set(WITH_PROTOCOL OFF)
set(WITH_EVPP ON)
set(WITH_HTTP ON)
set(WITH_HTTP_SERVER ON)
set(WITH_HTTP_CLIENT ON)
set(WITH_MQTT OFF)
set(WITH_OPENSSL OFF)
set(WITH_GNUTLS OFF)
set(WITH_MBEDTLS OFF)
set(WITH_KCP OFF)
set(WITH_WEPOLL ON)
set(ENABLE_WINDUMP OFF)
set(BUILD_FOR_MT ON)
add_subdirectory(3rd/libhv-1.3.4)
if(TARGET hv_static)
    set_target_properties(hv_static PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}")
endif()

# ---------- zlib 1.3.2（静态库 zlibstatic）----------
set(ZLIB_BUILD_SHARED OFF)
set(ZLIB_BUILD_STATIC ON)
set(ZLIB_BUILD_TESTING OFF)
set(ZLIB_INSTALL OFF)
add_subdirectory(3rd/zlib-1.3.2)

# ---------- MariaDB Connector/C 3.4.9（静态库 mariadbclient）----------
set(WITH_UNIT_TESTS OFF)
set(WITH_CURL OFF)
set(WITH_MSI OFF)
set(WITH_SIGNCODE OFF)
set(WITH_SSL ON)
set(WITH_EXTERNAL_ZLIB ON)
# 动态插件在作为子工程时没有安装目录，全部编进静态库
set(CLIENT_PLUGIN_DIALOG STATIC)
set(CLIENT_PLUGIN_CLIENT_ED25519 STATIC)
set(CLIENT_PLUGIN_CACHING_SHA2_PASSWORD STATIC)
set(CLIENT_PLUGIN_SHA256_PASSWORD STATIC)
set(CLIENT_PLUGIN_PARSEC STATIC)
set(CLIENT_PLUGIN_AUTH_GSSAPI_CLIENT STATIC)
set(CLIENT_PLUGIN_MYSQL_CLEAR_PASSWORD STATIC)
set(CLIENT_PLUGIN_PVIO_SHMEM STATIC)
set(ZLIB_FOUND TRUE)
set(ZLIB_LIBRARY ZLIB::ZLIBSTATIC)
set(ZLIB_LIBRARIES ZLIB::ZLIBSTATIC)
set(ZLIB_INCLUDE_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/3rd/zlib-1.3.2"
    "${CMAKE_CURRENT_SOURCE_DIR}/3rd/zlib-1.3.2")
add_subdirectory(3rd/mariadb-connector-c-3.4.9)
# 子工程路径下上游不会加这两个宏，winsock.h / winsock2.h 会冲突。只加在对象库上，避免裁掉 DuiLib 的 GDI。
if(TARGET mariadb_obj)
    target_compile_definitions(mariadb_obj PRIVATE WIN32_LEAN_AND_MEAN NOGDI)
endif()
if(TARGET libmariadb)
    set_target_properties(libmariadb PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()

set(GAMEV5_SOURCES
    ${GAMEV5_SRC_DIR}/main.cpp
    ${GAMEV5_SRC_DIR}/StdAfx.cpp
    ${GAMEV5_SRC_DIR}/MainWnd.cpp
    ${GAMEV5_SRC_DIR}/ServerConfig.cpp
    ${GAMEV5_SRC_DIR}/ServerAction.cpp
    ${GAMEV5_SRC_DIR}/GameV5.rc
    ${CMAKE_CURRENT_BINARY_DIR}/GameV5Zip.rc
)

file(GLOB_RECURSE GAMEV5_SKIN_FILES CONFIGURE_DEPENDS
    "${GAMEV5_SKIN_DIR}/*"
)

add_custom_target(GameV5SkinZip ALL
    BYPRODUCTS "${GAMEV5_SKIN_ZIP}"
    COMMAND ${CMAKE_COMMAND} -E rm -f "${GAMEV5_SKIN_ZIP}"
    COMMAND ${CMAKE_COMMAND} -E chdir "${GAMEV5_SKIN_DIR}"
        ${CMAKE_COMMAND} -E tar cf "${GAMEV5_SKIN_ZIP}" --format=zip .
    DEPENDS ${GAMEV5_SKIN_FILES}
    COMMENT "Pack GameV5 skin zip"
    VERBATIM
)

add_executable(GameV5 WIN32 ${GAMEV5_SOURCES})
add_dependencies(GameV5 GameV5SkinZip)
set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/GameV5Zip.rc PROPERTIES
    OBJECT_DEPENDS "${GAMEV5_SKIN_ZIP}"
)
set_source_files_properties(${GAMEV5_SRC_DIR}/GameV5.rc PROPERTIES
    OBJECT_DEPENDS "${GAMEV5_SRC_DIR}/App.ico"
)
target_include_directories(GameV5 PRIVATE
    ${GAMEV5_SRC_DIR}
    ${CMAKE_SOURCE_DIR}/3rd/mariadb-connector-c-3.4.9/include
    ${CMAKE_CURRENT_BINARY_DIR}/3rd/mariadb-connector-c-3.4.9/include
)
target_compile_definitions(GameV5 PRIVATE UNICODE _UNICODE)
target_compile_features(GameV5 PRIVATE cxx_std_17)
target_link_libraries(GameV5 PRIVATE
    lua55
    hv_static
    ZLIB::ZLIBSTATIC
    mariadbclient
    DuiLib
    lunasvg
    plutovg
    "${_WV2_LOADER}"
    d2d1 dwrite d3d11 dxgi dcomp windowscodecs
    version
    shell32
    shlwapi
    bcrypt
    crypt32
    comdlg32
    gdi32
    user32
    ole32
)

set_target_properties(GameV5 PROPERTIES
    DEBUG_POSTFIX "${CMAKE_DEBUG_POSTFIX}"
    RELEASE_POSTFIX "${CMAKE_RELEASE_POSTFIX}"
    MINSIZEREL_POSTFIX "${CMAKE_MINSIZEREL_POSTFIX}"
    RELWITHDEBINFO_POSTFIX "${CMAKE_RELWITHDEBINFO_POSTFIX}"
)
