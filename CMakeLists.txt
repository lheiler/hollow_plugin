cmake_minimum_required(VERSION 3.25)

project(Hollow VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(JUCE_DIR "$ENV{HOME}/toolchains/JUCE-9.0.2" CACHE PATH "Path to a JUCE checkout")
add_subdirectory("${JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")

option(HOLLOW_BUILD_SNAPSHOT_TOOL "Build the headless render/screenshot tool used for testing" ON)

# ------------------------------------------------------------------------------------------------
set(HOLLOW_SOURCES
    source/plugin/Parameters.cpp
    source/plugin/Presets.cpp
    source/plugin/Settings.cpp
    source/plugin/PresetLibrary.cpp
    source/plugin/PluginProcessor.cpp
    source/plugin/PluginEditor.cpp
    source/gui/Theme.cpp
    source/gui/Widgets.cpp
    source/gui/SpectrumAnalyzer.cpp
    source/gui/ChainStrip.cpp
    source/gui/PanelBase.cpp
    source/gui/FilterPanel.cpp
    source/gui/TrashPanel.cpp
    source/gui/ConvolvePanel.cpp
    source/gui/EffectPanels.cpp
    source/gui/ModulationPanel.cpp
    source/gui/MeterPanel.cpp
    source/gui/SettingsPage.cpp
    source/gui/PresetBrowser.cpp)

# Oxanium + Martian Mono (SIL Open Font License 1.1, see resources/fonts/OFL-*.txt)
juce_add_binary_data(HollowFonts
    HEADER_NAME HollowFonts.h
    NAMESPACE HollowFonts
    SOURCES
        resources/fonts/Oxanium-Light.ttf
        resources/fonts/Oxanium-Regular.ttf
        resources/fonts/Oxanium-SemiBold.ttf
        resources/fonts/MartianMono-Regular.ttf)

set(HOLLOW_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_MODAL_LOOPS_PERMITTED=0)

set(HOLLOW_MODULES
    HollowFonts
    juce::juce_audio_utils
    juce::juce_dsp)

# Macs also get an Audio Unit (Logic and GarageBand only load AUs)
if(APPLE)
    set(HOLLOW_FORMATS VST3 AU Standalone)
else()
    set(HOLLOW_FORMATS VST3 Standalone)
endif()

juce_add_plugin(Hollow
    COMPANY_NAME "lheiler"
    PRODUCT_NAME "Hollow"
    BUNDLE_ID "com.lheiler.hollow"
    PLUGIN_MANUFACTURER_CODE Lhei
    PLUGIN_CODE Hlw1
    FORMATS ${HOLLOW_FORMATS}
    VST3_CATEGORIES Fx Distortion
    AU_MAIN_TYPE kAudioUnitType_Effect
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    # The moduleinfo.json step runs the freshly built plugin, which is impossible when
    # cross-compiling. Hosts don't require the manifest.
    VST3_AUTO_MANIFEST FALSE)

target_sources(Hollow PRIVATE ${HOLLOW_SOURCES})
target_include_directories(Hollow PRIVATE source)
target_compile_definitions(Hollow PUBLIC ${HOLLOW_DEFINITIONS})
target_link_libraries(Hollow
    PRIVATE
        ${HOLLOW_MODULES}
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ------------------------------------------------------------------------------------------------
# Headless tool: instantiates the processor + editor, renders audio and writes editor screenshots.
if(HOLLOW_BUILD_SNAPSHOT_TOOL)
    juce_add_console_app(HollowSnapshot PRODUCT_NAME "HollowSnapshot")
    target_sources(HollowSnapshot PRIVATE tools/Snapshot.cpp tools/PresetPack.cpp ${HOLLOW_SOURCES})
    target_include_directories(HollowSnapshot PRIVATE source)
    target_compile_definitions(HollowSnapshot PRIVATE ${HOLLOW_DEFINITIONS} HOLLOW_SNAPSHOT_TOOL=1)
    target_link_libraries(HollowSnapshot
        PRIVATE
            ${HOLLOW_MODULES}
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()
