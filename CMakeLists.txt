cmake_minimum_required(VERSION 3.25)

project(IupacSynth2 VERSION 0.1.0 LANGUAGES C CXX)

include(CTest)
include(FetchContent)

option(IUPAC_ENABLE_CHEMISTRY "Build the chemistry extension" OFF)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 91ad83ae34a81e0833b1a2b0866f54846370ae53
    GIT_SHALLOW FALSE
    GIT_PROGRESS TRUE
)
FetchContent_MakeAvailable(JUCE)

add_library(iupac_synth_domain STATIC
    src/domain/Patch.cpp
    src/domain/ProductInfo.cpp
)
target_include_directories(iupac_synth_domain PUBLIC include)
target_compile_features(iupac_synth_domain PUBLIC cxx_std_20)
target_compile_definitions(iupac_synth_domain PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(iupac_synth_domain PUBLIC juce::juce_core)

if(IUPAC_ENABLE_CHEMISTRY)
    add_library(iupac_chemistry_analysis STATIC src/chemistry/Analysis.cpp src/chemistry/Mapping.cpp src/chemistry/Extension.cpp)
    target_include_directories(iupac_chemistry_analysis PUBLIC include)
    target_compile_features(iupac_chemistry_analysis PUBLIC cxx_std_20)
    target_compile_definitions(iupac_chemistry_analysis PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(iupac_chemistry_analysis PUBLIC iupac_synth_domain juce::juce_core)
endif()

add_library(iupac_synth_engine STATIC
    src/engine/Engine.cpp
    src/engine/Modules.cpp
    src/engine/PatchCoordinator.cpp
)
target_include_directories(iupac_synth_engine PUBLIC include)
target_compile_features(iupac_synth_engine PUBLIC cxx_std_20)
target_link_libraries(iupac_synth_engine PUBLIC
    iupac_synth_domain
    juce::juce_audio_basics
    juce::juce_dsp
)

add_library(iupac_synth_tools STATIC src/tools/Headless.cpp)
target_include_directories(iupac_synth_tools PUBLIC include)
target_compile_features(iupac_synth_tools PUBLIC cxx_std_20)
target_link_libraries(iupac_synth_tools PUBLIC iupac_synth_engine juce::juce_audio_formats juce::juce_cryptography)

add_executable(iupac-cli src/cli/Main.cpp)
target_compile_features(iupac-cli PRIVATE cxx_std_20)
target_link_libraries(iupac-cli PRIVATE iupac_synth_tools)
if(IUPAC_ENABLE_CHEMISTRY)
    target_link_libraries(iupac-cli PRIVATE iupac_chemistry_analysis)
    target_compile_definitions(iupac-cli PRIVATE IUPAC_ENABLE_CHEMISTRY=1)
endif()

if(APPLE)
    set(IUPAC_PLUGIN_FORMATS AU VST3 Standalone)
else()
    set(IUPAC_PLUGIN_FORMATS VST3 Standalone)
endif()

juce_add_plugin(IupacSynth2
    PRODUCT_NAME "IUPAC Synth 2"
    COMPANY_NAME "HighDA"
    COMPANY_WEBSITE "https://github.com/highda/iupac-synth-v2"
    COMPANY_EMAIL ""
    BUNDLE_ID "space.highda.iupacsynth2"
    PLUGIN_MANUFACTURER_CODE Iups
    PLUGIN_CODE Iup2
    FORMATS ${IUPAC_PLUGIN_FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
)
target_sources(IupacSynth2 PRIVATE
    src/plugin/PluginEditor.cpp
    src/plugin/PluginEditor.hpp
    src/plugin/PluginProcessor.cpp
    src/plugin/PluginProcessor.hpp
)
target_compile_features(IupacSynth2 PRIVATE cxx_std_20)
target_compile_features(IupacSynth2_VST3 PRIVATE cxx_std_20)
target_compile_features(IupacSynth2_Standalone PRIVATE cxx_std_20)
if(APPLE)
    target_compile_features(IupacSynth2_AU PRIVATE cxx_std_20)
endif()
target_compile_definitions(IupacSynth2 PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)
if(IUPAC_ENABLE_CHEMISTRY)
    target_compile_definitions(IupacSynth2 PRIVATE IUPAC_ENABLE_CHEMISTRY=1)
    target_link_libraries(IupacSynth2 PRIVATE iupac_chemistry_analysis)
endif()
target_link_libraries(IupacSynth2
    PRIVATE
        iupac_synth_engine
        juce::juce_audio_basics
        juce::juce_audio_processors
        juce::juce_audio_devices
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

if(BUILD_TESTING)
    add_executable(iupac-tests tests/ProductPathTests.cpp tests/PatchTests.cpp tests/ModuleTests.cpp tests/EngineTests.cpp tests/HeadlessTests.cpp tests/RealtimeAllocationTests.cpp)
    target_compile_features(iupac-tests PRIVATE cxx_std_20)
    target_link_libraries(iupac-tests PRIVATE iupac_synth_tools)
    add_test(NAME product-path-smoke COMMAND iupac-tests)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME render-distance-metric COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/test_render_distance.py)
    if(IUPAC_ENABLE_CHEMISTRY)
        add_executable(iupac-analysis-tests tests/AnalysisTests.cpp)
        target_compile_features(iupac-analysis-tests PRIVATE cxx_std_20)
        target_link_libraries(iupac-analysis-tests PRIVATE iupac_chemistry_analysis iupac_synth_engine)
        add_test(NAME analysis-schema COMMAND iupac-analysis-tests ${CMAKE_SOURCE_DIR}/tests/fixtures/chemistry-analysis-v1.json)
        add_executable(iupac-fake-chemistry-helper tests/FakeChemistryHelper.cpp)
        target_compile_features(iupac-fake-chemistry-helper PRIVATE cxx_std_20)
        add_executable(iupac-extension-tests tests/ExtensionTests.cpp)
        target_compile_features(iupac-extension-tests PRIVATE cxx_std_20)
        target_link_libraries(iupac-extension-tests PRIVATE iupac_chemistry_analysis iupac_synth_engine)
        add_dependencies(iupac-extension-tests iupac-fake-chemistry-helper)
        add_test(NAME chemistry-extension-supervisor COMMAND iupac-extension-tests $<TARGET_FILE:iupac-fake-chemistry-helper>)
        add_test(NAME chemistry-source-helper COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/chemistry/test_helper.py)
        add_test(NAME chemistry-panels COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/chemistry/test_panels.py)
        add_test(NAME discovery-data-index-cache COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/chemistry/test_discovery.py)
        add_test(NAME mapping-calibration COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/verify_mapping_calibration.py
            --cli $<TARGET_FILE:iupac-cli>
            --fixture ${CMAKE_SOURCE_DIR}/tests/fixtures/chemistry-analysis-v1.json
            --pairs ${CMAKE_SOURCE_DIR}/data/panels/legacy-hard-pairs.json
            --output-dir ${CMAKE_BINARY_DIR}/mapping-calibration)
        set_tests_properties(chemistry-source-helper chemistry-panels discovery-data-index-cache PROPERTIES ENVIRONMENT "IUPAC_OPSIN_JAR=${CMAKE_SOURCE_DIR}/third_party/opsin/opsin-cli-2.8.0.jar" TIMEOUT 120)
        set_tests_properties(mapping-calibration PROPERTIES TIMEOUT 600)
    endif()
    add_executable(iupac-plugin-tests tests/PluginAdapterTests.cpp)
    target_include_directories(iupac-plugin-tests PRIVATE src/plugin include)
    target_compile_features(iupac-plugin-tests PRIVATE cxx_std_20)
    target_compile_definitions(iupac-plugin-tests PRIVATE JucePlugin_Name="IUPAC Synth 2" JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(iupac-plugin-tests PRIVATE IupacSynth2 iupac_synth_engine juce::juce_audio_processors juce::juce_audio_utils)
    if(IUPAC_ENABLE_CHEMISTRY)
        target_compile_definitions(iupac-plugin-tests PRIVATE IUPAC_ENABLE_CHEMISTRY=1)
        add_dependencies(iupac-plugin-tests iupac-fake-chemistry-helper)
    endif()
    if(IUPAC_ENABLE_CHEMISTRY)
        add_test(NAME plugin-adapter COMMAND iupac-plugin-tests $<TARGET_FILE:iupac-fake-chemistry-helper>)
    else()
        add_test(NAME plugin-adapter COMMAND iupac-plugin-tests)
    endif()
    add_executable(iupac-editor-tests tests/EditorTests.cpp)
    target_include_directories(iupac-editor-tests PRIVATE src/plugin include)
    target_compile_features(iupac-editor-tests PRIVATE cxx_std_20)
    target_compile_definitions(iupac-editor-tests PRIVATE JucePlugin_Name="IUPAC Synth 2" JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(iupac-editor-tests PRIVATE IupacSynth2 iupac_synth_engine juce::juce_audio_processors juce::juce_audio_utils)
    if(IUPAC_ENABLE_CHEMISTRY)
        target_compile_definitions(iupac-editor-tests PRIVATE IUPAC_ENABLE_CHEMISTRY=1)
    endif()
    find_program(XVFB_RUN xvfb-run)
    if(XVFB_RUN)
        add_test(NAME native-editor-smoke COMMAND ${XVFB_RUN} -a $<TARGET_FILE:iupac-editor-tests>)
    endif()
endif()
