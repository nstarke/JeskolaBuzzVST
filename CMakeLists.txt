cmake_minimum_required(VERSION 3.25.0)

# Derive version from git tag (e.g. v1.2.3 -> 1.2.3.0)
# Falls back to 0.0.0.0 for non-tagged builds.
execute_process(
    COMMAND git describe --tags --abbrev=0
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_TAG
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE GIT_TAG_RESULT
)

if(GIT_TAG_RESULT EQUAL 0 AND GIT_TAG MATCHES "^v?([0-9]+)\\.([0-9]+)\\.([0-9]+)")
    set(PROJ_VERSION "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}.0")
else()
    set(PROJ_VERSION "0.0.0.0")
endif()

project(JeskolaBuzzVst
    VERSION ${PROJ_VERSION}
    DESCRIPTION "VST3 Wrapper for Jeskola Buzz Machine DLLs"
)

message(STATUS "[BuzzBridge] Version: ${PROJECT_VERSION}")

# Disable parts of the SDK we don't need
set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)
set(SMTG_RUN_VST_VALIDATOR OFF CACHE BOOL "" FORCE)
set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "" FORCE)

# Add VST3 SDK
add_subdirectory(sdk)

# Determine architecture
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(BUZZVST_64BIT TRUE)
    message(STATUS "[BuzzBridge] Building 64-bit VST3 plugin (uses bridge to load 32-bit machines)")
else()
    set(BUZZVST_64BIT FALSE)
    message(STATUS "[BuzzBridge] Building 32-bit VST3 plugin (loads machines directly)")
endif()

# =============================================================================
# Buzz layer sources (used in 32-bit builds and the 32-bit host)
# =============================================================================
set(BUZZ_SOURCES
    src/buzz/MachineInterfaceStubs.cpp
    src/buzz/BuzzMachineLoader.cpp
    src/buzz/BuzzCallbacks.cpp
    src/buzz/BuzzOscTables.cpp
    src/buzz/BuzzParamLayout.cpp
    src/buzz/BuzzWaveTable.cpp
    src/buzz/BuzzMDKHelper.cpp
)

# =============================================================================
# VST3 plugin sources
# =============================================================================
set(VST3_SOURCES
    src/vst3/BuzzProcessor.cpp
    src/vst3/BuzzController.cpp
    src/vst3/BuzzPluginView.cpp
    src/vst3/GearScanner.cpp
    src/vst3/GeneratorProcessor.cpp
    src/vst3/EffectProcessor.cpp
    src/vst3/ParameterMapping.cpp
    src/vst3/BuzzPluginFactory.cpp
)

if(BUZZVST_64BIT)
    # 64-bit build: include bridge client instead of direct Buzz loader
    set(BRIDGE_SOURCES
        src/bridge/BridgeClient.cpp
    )
    set(ALL_SOURCES ${BUZZ_SOURCES} ${VST3_SOURCES} ${BRIDGE_SOURCES})
else()
    set(ALL_SOURCES ${BUZZ_SOURCES} ${VST3_SOURCES})
endif()

# Create the VST3 plugin
smtg_add_vst3plugin(BuzzBridge ${ALL_SOURCES})
smtg_target_configure_version_file(BuzzBridge)

target_include_directories(BuzzBridge PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(BuzzBridge PRIVATE sdk comctl32)
target_compile_features(BuzzBridge PUBLIC cxx_std_17)

if(MSVC)
    target_compile_options(BuzzBridge PRIVATE /EHa)
    target_compile_definitions(BuzzBridge PRIVATE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        BUZZVST_VERSION_STR="${PROJECT_VERSION}"
    )
endif()

if(BUZZVST_64BIT)
    target_compile_definitions(BuzzBridge PRIVATE BUZZVST_64BIT=1)
endif()

if(SMTG_WIN)
    target_link_libraries(BuzzBridge PRIVATE kernel32 comdlg32 user32 gdi32 shell32 ole32)
endif()

if(SMTG_MAC)
    smtg_target_set_bundle(BuzzBridge
        BUNDLE_IDENTIFIER "com.jeskolabuzzvst.buzzbridge"
        COMPANY_NAME "JeskolaBuzzVst"
    )
endif()

# =============================================================================
# 32-bit Bridge Host Executable (only built in 32-bit configuration)
# =============================================================================
if(NOT BUZZVST_64BIT)
    add_executable(BuzzBridgeHost32
        src/bridge/BuzzBridgeHost32.cpp
        ${BUZZ_SOURCES}
    )

    target_include_directories(BuzzBridgeHost32 PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    target_compile_features(BuzzBridgeHost32 PUBLIC cxx_std_17)

    if(MSVC)
        target_compile_options(BuzzBridgeHost32 PRIVATE /EHa)
        target_compile_definitions(BuzzBridgeHost32 PRIVATE
            _CRT_SECURE_NO_WARNINGS
            NOMINMAX
        )
    endif()

    set_target_properties(BuzzBridgeHost32 PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
endif()

# =============================================================================
# Test Suite (only in 32-bit builds where we can load Buzz DLLs directly)
# =============================================================================
if(NOT BUZZVST_64BIT)
    enable_testing()

    set(TEST_SOURCES
        tests/main.cpp
        tests/test_BuzzParamLayout.cpp
        tests/test_ParameterMapping.cpp
        tests/test_BuzzOscTables.cpp
        tests/test_BuzzCallbacks.cpp
        tests/test_BuzzMachineLoader.cpp
        tests/test_Midi.cpp
        tests/test_GearScanner.cpp
        tests/test_HiDPI.cpp
        tests/test_WaveTable.cpp
        tests/test_MultiTrack.cpp
        tests/test_ParamSliders.cpp
        tests/test_Validation.cpp
        tests/test_Comprehensive.cpp
        tests/test_MDK.cpp
        tests/test_BridgeAndAudio.cpp
    )

    add_executable(BuzzBridgeTests
        ${TEST_SOURCES}
        src/buzz/MachineInterfaceStubs.cpp
        src/buzz/BuzzMachineLoader.cpp
        src/buzz/BuzzCallbacks.cpp
        src/buzz/BuzzOscTables.cpp
        src/buzz/BuzzParamLayout.cpp
        src/buzz/BuzzWaveTable.cpp
        src/buzz/BuzzMDKHelper.cpp
        src/vst3/ParameterMapping.cpp
        src/vst3/GearScanner.cpp
        src/vst3/BuzzPluginView.cpp
    )

    target_include_directories(BuzzBridgeTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${CMAKE_CURRENT_SOURCE_DIR}/sdk
        ${CMAKE_CURRENT_SOURCE_DIR}/sdk/pluginterfaces
    )

    target_compile_features(BuzzBridgeTests PUBLIC cxx_std_17)

    # Link VST3 SDK (needed for CPluginView, FObject, etc. used by HiDPI tests)
    target_link_libraries(BuzzBridgeTests PRIVATE sdk)

    if(MSVC)
        target_compile_options(BuzzBridgeTests PRIVATE /EHa)
        target_compile_definitions(BuzzBridgeTests PRIVATE
            _CRT_SECURE_NO_WARNINGS
            NOMINMAX
        )
    endif()

    if(SMTG_WIN)
        target_link_libraries(BuzzBridgeTests PRIVATE
            kernel32 comdlg32 user32 gdi32 shell32 ole32 comctl32
        )
    endif()

    set_target_properties(BuzzBridgeTests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )

    add_test(NAME BuzzBridgeTests COMMAND BuzzBridgeTests)

    # Standalone work test (for debugging audio issues)
    add_executable(WorkTest
        tests/test_WorkDirect.cpp
        ${BUZZ_SOURCES}
        src/bridge/BridgeClient.cpp
    )
    target_include_directories(WorkTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_features(WorkTest PUBLIC cxx_std_17)
    if(MSVC)
        target_compile_options(WorkTest PRIVATE /EHa)
        target_compile_definitions(WorkTest PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
    endif()
    set_target_properties(WorkTest PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()
