cmake_minimum_required(VERSION 3.22)

set(RB_VERSION_SUFFIX "beta.1" CACHE STRING "Pre-release suffix appended to the version (empty for a final release)")
project(ReverseBack VERSION 0.1.0 LANGUAGES C CXX)

option(RB_CORE_ONLY "Build only the framework-free core library and its tests (no JUCE, no network)" OFF)
option(RB_BUILD_PLUGIN "Build the JUCE Standalone / VST3 / CLAP targets" ON)
option(RB_BUILD_TESTS "Build tests" ON)
set(RB_SANITIZE "" CACHE STRING "Semicolon list for core tests: address;undefined or thread")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(RB_VERSION_SUFFIX)
  set(RB_VERSION_STRING "${PROJECT_VERSION}-${RB_VERSION_SUFFIX}")
else()
  set(RB_VERSION_STRING "${PROJECT_VERSION}")
endif()
message(STATUS "ReverseBack ${RB_VERSION_STRING}")

# ---------------------------------------------------------------- core (no JUCE)
add_library(rb_core STATIC
  Source/Core/Resampler.cpp
  Source/Core/ClipPlayer.cpp
  Source/Core/SignalTools.cpp
  Source/Core/Stages.cpp
  Source/Core/RecordTransport.cpp
  Source/Core/LiveTransport.cpp
  Source/Core/Engine.cpp
  Source/Core/Presets.cpp
  Source/Core/OfflineRender.cpp
  Source/Core/DiskClipSource.cpp)
target_include_directories(rb_core PUBLIC Source/Core)
target_compile_features(rb_core PUBLIC cxx_std_20)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(rb_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
  if(RB_SANITIZE)
    string(REPLACE ";" "," _san "${RB_SANITIZE}")
    target_compile_options(rb_core PUBLIC -fsanitize=${_san} -fno-omit-frame-pointer -g)
    target_link_options(rb_core PUBLIC -fsanitize=${_san})
    target_compile_definitions(rb_core PUBLIC RB_NO_ALLOC_HOOK=1)
  endif()
elseif(MSVC)
  target_compile_options(rb_core PRIVATE /W4 /wd4324)   # 4324: cache-line alignment padding is intended
endif()

if(RB_BUILD_TESTS)
  enable_testing()
  add_executable(rb_core_tests
    Tests/Core/TestMain.cpp
    Tests/Core/ClipTests.cpp
    Tests/Core/ResamplerTests.cpp
    Tests/Core/SignalToolsTests.cpp
    Tests/Core/RecordTests.cpp
    Tests/Core/LiveTests.cpp
    Tests/Core/EngineTests.cpp
    Tests/Core/StressTests.cpp
    Tests/Core/DiskTests.cpp
    Tests/Core/AuditTests.cpp)
  target_link_libraries(rb_core_tests PRIVATE rb_core)
  find_package(Threads REQUIRED)
  target_link_libraries(rb_core PUBLIC Threads::Threads)
  target_include_directories(rb_core_tests PRIVATE Tests/Core)
  add_test(NAME rb_core_tests COMMAND rb_core_tests)
endif()

# ---------------------------------------------------------------- JUCE targets
if(NOT RB_CORE_ONLY AND RB_BUILD_PLUGIN)
  include(cmake/Plugin.cmake)
endif()
