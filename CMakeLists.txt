cmake_minimum_required(VERSION 3.24)

project(typed_scanner
  VERSION 0.1.0
  DESCRIPTION "Typed Scanner: CSV & NDJSON tokenizer + static report server"
  LANGUAGES CXX)

option(TS_BUILD_TESTS "Build test binaries" ON)
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

# ---- deps -------------------------------------------------------------------
FetchContent_Declare(simdjson
  GIT_REPOSITORY https://github.com/simdjson/simdjson.git
  GIT_TAG        v3.13.0)
FetchContent_MakeAvailable(simdjson)

FetchContent_Declare(tomlplusplus
  GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
  GIT_TAG        v3.4.0)
FetchContent_MakeAvailable(tomlplusplus)

FetchContent_Declare(fast_float
  GIT_REPOSITORY https://github.com/fastfloat/fast_float.git
  GIT_TAG        v8.0.2)
FetchContent_MakeAvailable(fast_float)
if(NOT TARGET FastFloat::fast_float)
  get_filename_component(_ff_inc "${fast_float_SOURCE_DIR}/include" ABSOLUTE)
  ts_make_header_only(FastFloat::fast_float "${_ff_inc}")
endif()

FetchContent_Declare(cpp_httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG        v0.18.7)
FetchContent_MakeAvailable(cpp_httplib)
if(NOT TARGET cpp-httplib)
  get_filename_component(_httplib_inc "${cpp_httplib_SOURCE_DIR}" ABSOLUTE)
  ts_make_header_only(cpp-httplib "${_httplib_inc}")
endif()

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
  "${kainjow_mustache_SOURCE_DIR}")
add_library(kainjow::mustache ALIAS kainjow_mustache)

# ---- sources ----------------------------------------------------------------
file(GLOB_RECURSE TS_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/**/*.cpp")
if(NOT TS_ENABLE_JSONL)
  list(FILTER TS_SOURCES EXCLUDE REGEX ".*/src/scanner/token_jsonl_simdjson.cpp$")
endif()

set(TS_MAIN_SRC "${CMAKE_SOURCE_DIR}/src/main/main.cpp")
set(TS_CORE_SOURCES ${TS_SOURCES})
list(REMOVE_ITEM TS_CORE_SOURCES "${TS_MAIN_SRC}")

# ---- core library -----------------------------------------------------------
add_library(ts_core STATIC ${TS_CORE_SOURCES})
target_include_directories(ts_core PUBLIC "${CMAKE_SOURCE_DIR}/include" "${CMAKE_SOURCE_DIR}")
target_link_libraries(ts_core PUBLIC
  simdjson::simdjson tomlplusplus::tomlplusplus FastFloat::fast_float cpp-httplib kainjow::mustache)

# Warnings / Sanitizers
if(MSVC)
  target_compile_options(ts_core PRIVATE /permissive- /Zc:__cplusplus /W4)
else()
  target_compile_options(ts_core PRIVATE -Wall -Wextra -Wpedantic)
  if(TS_WARNINGS_AS_ERRORS)
    target_compile_options(ts_core PRIVATE -Werror)
  endif()
  if(TS_ENABLE_SANITIZERS AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(ts_core PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(ts_core PRIVATE -fsanitize=address,undefined)
  endif()
endif()

target_compile_definitions(ts_core PUBLIC
  TS_DEFAULT_CONFIG_DIR="configs"
  TS_DEFAULT_TEMPLATE_DIR="templates"
  TS_DEFAULT_STATIC_DIR="web")

# Optional OpenSSL
find_package(OpenSSL QUIET)
if(OpenSSL_FOUND)
  message(STATUS "OpenSSL found: enabling TS_USE_OPENSSL")
  target_compile_definitions(ts_core PUBLIC TS_USE_OPENSSL=1)
  target_link_libraries(ts_core PUBLIC OpenSSL::Crypto)
else()
  message(STATUS "OpenSSL not found: using std::hash fallback")
endif()

# ---- app --------------------------------------------------------------------
add_executable(typed-scanner ${TS_MAIN_SRC})
target_link_libraries(typed-scanner PRIVATE ts_core)

# Install app & assets
include(GNUInstallDirs)
install(TARGETS typed-scanner RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY templates/ DESTINATION ${CMAKE_INSTALL_DATADIR}/typed-scanner/templates)
install(DIRECTORY web/       DESTINATION ${CMAKE_INSTALL_DATADIR}/typed-scanner/web)
install(DIRECTORY configs/   DESTINATION ${CMAKE_INSTALL_DATADIR}/typed-scanner/configs)

# ---- tests ------------------------------------------------------------------
if(TS_BUILD_TESTS)
  function(ts_add_unit exe src)
    add_executable(${exe} "tests/unit/${src}")
    target_include_directories(${exe} PRIVATE "${CMAKE_SOURCE_DIR}/include")
    target_link_libraries(${exe} PRIVATE ts_core)
    install(TARGETS ${exe} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    message(STATUS "Unit test enabled: ${exe} (${src})")
  endfunction()

  function(ts_add_it exe src)
    set(full "${CMAKE_SOURCE_DIR}/${src}")
    if(EXISTS "${full}")
      add_executable(${exe} "${src}")
      target_include_directories(${exe} PRIVATE "${CMAKE_SOURCE_DIR}/include")
      target_link_libraries(${exe} PRIVATE ts_core)
      install(TARGETS ${exe} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
      message(STATUS "Integration test enabled: ${exe} (${src})")
    else()
      message(WARNING "Skipping integration test: ${full} not found")
    endif()
  endfunction()

  # Unit tests
  ts_add_unit(ts_test_chunk_reader     test_chunk_reader.cpp)
  ts_add_unit(ts_test_csv_fsm          test_csv_fsm.cpp)
  ts_add_unit(ts_test_jsonl_tokenizer  test_jsonl_tokenizer.cpp)
  ts_add_unit(ts_test_parse_policy     test_parse_policy.cpp)
  ts_add_unit(ts_test_record_view      test_record_view.cpp)
  ts_add_unit(ts_test_run_json         test_run_json.cpp)

  # Integration tests
  ts_add_it(ts_it_end_to_end_csv       tests/integration/test_end_to_end_csv.cpp)
  ts_add_it(ts_it_end_to_end_jsonl     tests/integration/test_end_to_end_jsonl.cpp)
  ts_add_it(ts_it_http_server          tests/integration/test_http_server.cpp)

  # Ship test data
  if(EXISTS "${CMAKE_SOURCE_DIR}/tests/data")
    install(DIRECTORY tests/data/ DESTINATION ${CMAKE_INSTALL_DATADIR}/typed-scanner/tests/data)
    message(STATUS "Installed test fixtures to share/typed-scanner/tests/data")
  endif()
endif()


# Make TS_ENABLE_JSONL visible to code as 0/1
target_compile_definitions(ts_core PUBLIC TS_ENABLE_JSONL=$<BOOL:${TS_ENABLE_JSONL}>)

# ---- benches ---------------------------------------------------------------
if(TS_BUILD_BENCH)
  function(ts_add_bench exe src)
    add_executable(${exe} "bench/${src}")
    target_include_directories(${exe} PRIVATE "${CMAKE_SOURCE_DIR}/include")
    target_link_libraries(${exe} PRIVATE ts_core)
    install(TARGETS ${exe} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    message(STATUS "Benchmark enabled: ${exe} (${src})")
  endfunction()

  ts_add_bench(ts_bench_tokenizer   tokenizer_bench.cpp)
  ts_add_bench(ts_bench_policy      policy_bench.cpp)
  ts_add_bench(ts_bench_arena_alloc arena_alloc_bench.cpp)
endif()
