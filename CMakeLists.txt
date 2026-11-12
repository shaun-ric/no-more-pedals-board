cmake_minimum_required(VERSION 3.22)
project(RigBench VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# ---- Options ---------------------------------------------------------------
option(RIGBENCH_AVX2 "Build with AVX2 (the mini PC's Intel N97 supports it)" ON)
option(RIGBENCH_NAM_FAST_TANH "Enable NAM's fast tanh approximation" ON)
set(ASIO_SDK_DIR "" CACHE PATH "Steinberg ASIO SDK folder (the one containing 'common')")
set(NAM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/NeuralAmpModelerCore"
    CACHE PATH "NeuralAmpModelerCore checkout (cloned with --recursive)")

# ---- NAM core --------------------------------------------------------------
if(NOT EXISTS "${NAM_DIR}/NAM")
  message(FATAL_ERROR "NeuralAmpModelerCore not found at ${NAM_DIR}. See README.md step 1.")
endif()
if(NOT EXISTS "${NAM_DIR}/Dependencies/eigen/Eigen")
  message(FATAL_ERROR "NAM core is missing its Eigen submodule. Run: git -C ${NAM_DIR} submodule update --init --recursive")
endif()

# ---- JUCE ------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.6
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# Same CPU flags for every target so Eigen types agree across translation units.
if(MSVC)
  set(RB_FLAGS /bigobj /utf-8)
  if(RIGBENCH_AVX2)
    list(APPEND RB_FLAGS /arch:AVX2)
  endif()
else()
  set(RB_FLAGS "")
  if(RIGBENCH_AVX2)
    list(APPEND RB_FLAGS -mavx2 -mfma)
  endif()
endif()

file(GLOB_RECURSE NAM_SOURCES CONFIGURE_DEPENDS "${NAM_DIR}/NAM/*.cpp")
add_library(nam_core OBJECT ${NAM_SOURCES})
target_include_directories(nam_core PUBLIC
  "${NAM_DIR}"
  "${NAM_DIR}/Dependencies"
  "${NAM_DIR}/Dependencies/eigen"
  "${NAM_DIR}/Dependencies/nlohmann")
target_compile_definitions(nam_core PUBLIC NAM_SAMPLE_FLOAT NOMINMAX _USE_MATH_DEFINES)
target_compile_options(nam_core PRIVATE ${RB_FLAGS})

# ---- App -------------------------------------------------------------------
juce_add_gui_app(RigBench
  PRODUCT_NAME "RigBench"
  COMPANY_NAME "RigHost"
  VERSION 0.1.0)

target_sources(RigBench PRIVATE
  Source/Main.cpp
  Source/MainComponent.cpp
  Source/AudioEngine.cpp
  Source/Core/Graph.cpp
  Source/Core/Library.cpp
  Source/Core/Rig.cpp
  Source/Nodes/AmpNodes.cpp
  Source/Nodes/NodeFactory.cpp
  Source/Nodes/VstNode.cpp
  Source/Nodes/MidiOutNode.cpp
  Source/MidiRouter.cpp
  Source/SceneManager.cpp
  Source/NamMetadata.cpp
  Source/NamProcessor.cpp)

target_compile_definitions(RigBench PRIVATE
  JUCE_WEB_BROWSER=0
  JUCE_PLUGINHOST_VST3=1
  JUCE_USE_CURL=0
  JUCE_WASAPI_EXCLUSIVE=1
  JUCE_APPLICATION_NAME_STRING="RigBench"
  JUCE_APPLICATION_VERSION_STRING="0.1.0")

if(RIGBENCH_NAM_FAST_TANH)
  target_compile_definitions(RigBench PRIVATE RIGBENCH_NAM_FAST_TANH=1)
endif()

target_compile_options(RigBench PRIVATE ${RB_FLAGS})

if(WIN32)
  if(ASIO_SDK_DIR)
    target_compile_definitions(RigBench PRIVATE JUCE_ASIO=1)
    target_include_directories(RigBench PRIVATE "${ASIO_SDK_DIR}/common")
  else()
    message(WARNING "ASIO_SDK_DIR not set: building without ASIO (WASAPI only). "
                    "Latency will not be representative of the final app.")
  endif()
endif()

target_link_libraries(RigBench PRIVATE
  nam_core
  juce::juce_gui_basics
  juce::juce_audio_utils
  juce::juce_audio_devices
  juce::juce_audio_processors
  juce::juce_dsp
  juce::juce_recommended_config_flags)
