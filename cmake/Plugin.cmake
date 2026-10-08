# JUCE Standalone / VST3 / CLAP targets (included from the top-level CMakeLists unless RB_CORE_ONLY).
include(FetchContent)

# Pinned dependencies (BUILD_RELEASE.md section 1). Override with -DFETCHCONTENT_SOURCE_DIR_JUCE=<dir> for offline builds.
FetchContent_Declare(juce
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.15
  GIT_SHALLOW TRUE)
# The newest tag (0.26.0) predates JUCE 8.0.11; this pinned commit adds support for juce_audio_processors_headless.
FetchContent_Declare(clap_juce_extensions
  GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
  GIT_TAG 7adee3a1bd4684d4caa5601100e364abccff4b4d)
FetchContent_MakeAvailable(juce)
FetchContent_MakeAvailable(clap_juce_extensions)

# ---------------------------------------------------------------- embedded assets (fonts, icon)
juce_add_binary_data(rb_assets NAMESPACE RBAssets SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/Assets/Inter-Regular.otf
  ${CMAKE_CURRENT_SOURCE_DIR}/Assets/Inter-Medium.otf
  ${CMAKE_CURRENT_SOURCE_DIR}/Assets/Inter-SemiBold.otf
  ${CMAKE_CURRENT_SOURCE_DIR}/Assets/Inter-Bold.otf
  ${CMAKE_CURRENT_SOURCE_DIR}/Assets/icon-256.png)
set_target_properties(rb_assets PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------- the plugin
set(RB_FORMATS Standalone VST3)
juce_add_plugin(ReverseBack
  COMPANY_NAME "Circuit Drift Labs"
  COMPANY_WEBSITE "https://github.com/djshellshoxxx/reverseback"
  PRODUCT_NAME "ReverseBack"
  BUNDLE_ID "labs.circuitdrift.reverseback"
  PLUGIN_MANUFACTURER_CODE CdLb
  PLUGIN_CODE RvBk
  FORMATS ${RB_FORMATS}
  VERSION ${PROJECT_VERSION}
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE
  VST3_CATEGORIES Fx Tools
  COPY_PLUGIN_AFTER_BUILD FALSE
  ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Assets/icon-256.png")

file(GLOB RB_PLUGIN_SOURCES CONFIGURE_DEPENDS Source/Audio/*.cpp Source/IO/*.cpp Source/UI/*.cpp)
target_sources(ReverseBack PRIVATE ${RB_PLUGIN_SOURCES})

target_include_directories(ReverseBack PUBLIC Source Source/Core Source/Audio Source/IO Source/UI)

target_compile_definitions(ReverseBack PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_REPORT_APP_USAGE=0
  JUCE_STRICT_REFCOUNTEDPOINTER=1
  JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1
  $<$<PLATFORM_ID:Linux>:JUCE_ALSA=1>   # audio back-ends that exist only on Linux (JACK headers do not on Windows)
  $<$<PLATFORM_ID:Linux>:JUCE_JACK=1>
  JUCE_MODAL_LOOPS_PERMITTED=0
  RB_VERSION_STRING="${RB_VERSION_STRING}")

# MSVC: padding caused by the cache-line alignment of the queue counters is intended
target_compile_options(ReverseBack PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/wd4324>)

target_link_libraries(ReverseBack
  PRIVATE
    rb_core
    rb_assets
    juce::juce_audio_utils
    juce::juce_audio_formats
    juce::juce_gui_extra
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# The custom standalone application lives only in the standalone target.
target_sources(ReverseBack_Standalone PRIVATE Source/Standalone/StandaloneApp.cpp)

# ---------------------------------------------------------------- CLAP
clap_juce_extensions_plugin(TARGET ReverseBack
  CLAP_ID "labs.circuitdrift.reverseback"
  CLAP_FEATURES audio-effect utility stereo
  CLAP_SUPPORT_URL "https://github.com/djshellshoxxx/reverseback")

# Export only the format entry points from the plugin binaries (Linux; other platforms use their own mechanisms).
if(UNIX AND NOT APPLE)
  target_link_options(ReverseBack_CLAP PRIVATE "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/cmake/exports-clap.map")
  target_link_options(ReverseBack_VST3 PRIVATE "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/cmake/exports-vst3.map")
endif()

# ---------------------------------------------------------------- integration tests
if(RB_BUILD_TESTS)
  file(GLOB RB_INTEGRATION_SOURCES CONFIGURE_DEPENDS Tests/Integration/*.cpp)
  if(RB_INTEGRATION_SOURCES)
    add_executable(rb_integration_tests Tests/Core/TestMain.cpp ${RB_INTEGRATION_SOURCES})
    target_include_directories(rb_integration_tests PRIVATE Tests/Core Tests/Integration Source Source/Core Source/Audio Source/IO Source/UI)
    target_compile_definitions(rb_integration_tests PRIVATE
      JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1 RB_JUCE_TESTS=1
      RB_VERSION_STRING="${RB_VERSION_STRING}")
    target_link_libraries(rb_integration_tests PRIVATE
      ReverseBack rb_core rb_assets
      juce::juce_audio_utils juce::juce_audio_formats juce::juce_gui_extra
      juce::juce_recommended_config_flags)
    find_program(RB_XVFB xvfb-run)
    if(RB_XVFB)
      add_test(NAME rb_integration_tests COMMAND ${RB_XVFB} -a $<TARGET_FILE:rb_integration_tests>)
    else()
      add_test(NAME rb_integration_tests COMMAND rb_integration_tests)
    endif()
  endif()
endif()
