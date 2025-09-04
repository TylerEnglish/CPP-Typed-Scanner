cmake_minimum_required(VERSION 3.24)

project(typed_scanner
  VERSION 0.1.0
  DESCRIPTION "Typed Scanner: CSV & NDJSON tokenizer + static report server"
  LANGUAGES CXX)

option(TS_BUILD_TESTS "Build unit tests" ON)
option(TS_BUILD_BENCH "Build micro-benchmarks" ON)
option(TS_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
option(TS_ENABLE_SANITIZERS "Enable ASAN/UBSAN (non-Windows)" OFF)
option(TS_ENABLE_JSONL "Build JSONL (simdjson) tokenizer" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Avoid dependency subprojects enabling their own tests
set(BUILD_TESTING OFF CACHE BOOL "Disable tests in dependencies" FORCE)

include(FetchContent)

function(ts_make_header_only name include_dir)
  add_library(${name} INTERFACE)
  target_include_directories(${name} INTERFACE "${include_dir}")
endfunction()

# simdjson
FetchContent_Declare(simdjson
  GIT_REPOSITORY https://github.com/simdjson/simdjson.git
  GIT_TAG        v3.13.0)
FetchContent_MakeAvailable(simdjson)

# toml++
FetchContent_Declare(tomlplusplus
  GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
  GIT_TAG        v3.4.0)
FetchContent_MakeAvailable(tomlplusplus)

# fast_float (header-only)
FetchContent_Declare(fast_float
  GIT_REPOSITORY https://github.com/fastfloat/fast_float.git
  GIT_TAG        v8.0.2)
FetchContent_MakeAvailable(fast_float)
if(NOT TARGET FastFloat::fast_float)
  get_filename_component(_ff_inc "${fast_float_SOURCE_DIR}/include" ABSOLUTE)
  ts_make_header_only(FastFloat::fast_float "${_ff_inc}")
endif()

# cpp-httplib (header-only)
FetchContent_Declare(cpp_httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG        v0.18.7)
FetchContent_MakeAvailable(cpp_httplib)
if(NOT TARGET cpp-httplib)
  get_filename_component(_httplib_inc "${cpp_httplib_SOURCE_DIR}" ABSOLUTE)
  ts_make_header_only(cpp-httplib "${_httplib_inc}")
endif()

# kainjow/mustache (header-only)
FetchContent_Declare(kainjow_mustache
  GIT_REPOSITORY https://github.com/kainjow/Mustache.git
  GIT_TAG        v4.1)
FetchContent_GetProperties(kainjow_mustache)
if(NOT kainjow_mustache_POPULATED)
  FetchContent_Populate(kainjow_mustache)
endif()
add_library(kainjow_mustache INTERFACE)
target_include_directories(kainjow_mustache INTERFACE
  "${kainjow_mustache_SOURCE_DIR}/include"
  "${kainjow_mustache_SOURCE_DIR}"
)
add_library(kainjow::mustache ALIAS kainjow_mustache)

# Sources (recursive)
file(GLOB_RECURSE TS_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/**/*.cpp")

# Optionally exclude JSONL tokenizer while adapting to simdjson v3
if(NOT TS_ENABLE_JSONL)
  list(FILTER TS_SOURCES EXCLUDE REGEX ".*/src/scanner/token_jsonl_simdjson.cpp$")
endif()

add_executable(typed-scanner ${TS_SOURCES})

# Support both #include "typed_scanner/..." and legacy "include/typed_scanner/..."
target_include_directories(typed-scanner PRIVATE
  "${CMAKE_SOURCE_DIR}/include"
  "${CMAKE_SOURCE_DIR}"
)

target_link_libraries(typed-scanner PRIVATE
  simdjson::simdjson
  tomlplusplus::tomlplusplus
  FastFloat::fast_float
  cpp-httplib
  kainjow::mustache
)

# Warnings / Sanitizers
if(MSVC)
  target_compile_options(typed-scanner PRIVATE /permissive- /Zc:__cplusplus /W4)
  if(TS_WARNINGS_AS_ERRORS)
    target_compile_options(typed-scanner PRIVATE /WX)
  endif()
else()
  target_compile_options(typed-scanner PRIVATE -Wall -Wextra -Wpedantic)
  if(TS_WARNINGS_AS_ERRORS)
    target_compile_options(typed-scanner PRIVATE -Werror)
  endif()
  if(TS_ENABLE_SANITIZERS AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(typed-scanner PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(typed-scanner PRIVATE -fsanitize=address,undefined)
  endif()
endif()

# Built-in default dirs
target_compile_definitions(typed-scanner PRIVATE
  TS_DEFAULT_CONFIG_DIR="configs"
  TS_DEFAULT_TEMPLATE_DIR="templates"
  TS_DEFAULT_STATIC_DIR="web"
)

# Optional OpenSSL for SHA-256 in path_utils
find_package(OpenSSL QUIET)
if(OpenSSL_FOUND)
  message(STATUS "OpenSSL found: enabling TS_USE_OPENSSL")
  target_compile_definitions(typed-scanner PRIVATE TS_USE_OPENSSL=1)
  target_link_libraries(typed-scanner PRIVATE OpenSSL::Crypto)
else()
  message(STATUS "OpenSSL not found: using std::hash fallback")
endif()

include(GNUInstallDirs)
install(TARGETS typed-scanner RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY templates/ DESTINATION ${CMAKE_INSTALL_DATADIR}/typed-scanner/templates)
install(DIRECTORY web/       DESTINATION ${CMAKE_INSTALL_DATADIR}/typed-scanner/web)
install(DIRECTORY configs/   DESTINATION ${CMAKE_INSTALL_DATADIR}/typed-scanner/configs)
