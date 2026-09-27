cmake_minimum_required(VERSION 3.18)
project(gm_resource_reservations LANGUAGES C CXX)
if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "Configure the native resource tests for Windows Win32")
endif()
include(CTest)
include(FetchContent)
# Same pinned Lua 5.4.6 source and static test-host pattern as Recorder's
# framework-settings compatibility suite; no game files are downloaded in CI.
FetchContent_Declare(lua_source
  GIT_REPOSITORY https://github.com/lua/lua.git
  GIT_TAG 6443185167c77adcc8552a3fee7edab7895db1a9
  SOURCE_SUBDIR no-cmake-project)
FetchContent_MakeAvailable(lua_source)
file(GLOB lua_files "${lua_source_SOURCE_DIR}/*.c")
list(FILTER lua_files EXCLUDE REGEX "/(lua|luac|onelua|ltests)\\.c$")
add_library(test_lua STATIC ${lua_files})
target_include_directories(test_lua PUBLIC "${lua_source_SOURCE_DIR}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/lua.hpp"
  "extern \"C\" {\n#include \"lua.h\"\n#include \"lauxlib.h\"\n#include \"lualib.h\"\n}\n")
set(owner "${CMAKE_CURRENT_SOURCE_DIR}/../ucp_gmResourceModifier/ucp_gmResourceModifier")
add_executable(reservation_check reservations.cpp "${owner}/gmResourceModifier.cpp")
target_compile_features(reservation_check PRIVATE cxx_std_17)
target_include_directories(reservation_check PRIVATE stubs "${owner}" "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(reservation_check PRIVATE test_lua)
foreach(scenario success mismatch slot-capacity image-capacity occupied bad-offset)
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${scenario}")
  add_test(NAME ${scenario} COMMAND reservation_check ${scenario})
  set_tests_properties(${scenario} PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${scenario}")
endforeach()
