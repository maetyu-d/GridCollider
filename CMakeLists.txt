cmake_minimum_required(VERSION 3.22)

project(GridCollider
    VERSION 0.1.0
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.12
    GIT_SHALLOW TRUE)

FetchContent_MakeAvailable(JUCE)

juce_add_gui_app(GridCollider
    PRODUCT_NAME "GridCollider"
    COMPANY_NAME "GridCollider"
    BUNDLE_ID "com.gridcollider.app"
    VERSION ${PROJECT_VERSION})

file(GLOB GRIDCOLLIDER_EXAMPLES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/*.orca"
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/*.gridcollider")

target_sources(GridCollider
    PRIVATE
        src/Main.cpp
        src/MainComponent.cpp
        src/MainComponent.h
        src/Core/EventRouter.cpp
        src/Core/EventRouter.h
        src/Core/GridInterpreter.cpp
        src/Core/GridInterpreter.h
        src/Core/GridModel.cpp
        src/Core/GridModel.h
        src/Core/TransportEngine.cpp
        src/Core/TransportEngine.h
        src/Components/GridEditorComponent.cpp
        src/Components/GridEditorComponent.h
        src/Components/StateGraphComponent.cpp
        src/Components/StateGraphComponent.h
        src/IO/EmbeddedScAudioEngine.cpp
        src/IO/EmbeddedScAudioEngine.h
        src/IO/OscOutput.cpp
        src/IO/OscOutput.h
        src/Presets/PresetManager.cpp
        src/Presets/PresetManager.h
        src/Utility/StatusLog.cpp
        src/Utility/StatusLog.h
        ${GRIDCOLLIDER_EXAMPLES})

add_custom_command(TARGET GridCollider POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:GridCollider>/Resources/examples"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/examples"
        "$<TARGET_BUNDLE_CONTENT_DIR:GridCollider>/Resources/examples")

target_compile_features(GridCollider PRIVATE cxx_std_20)

target_compile_definitions(GridCollider
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_PLUGINHOST_VST3=1
        GRIDCOLLIDER_ALCHEMY_SC_ROOT="/Users/user/Documents/weld2/ChucK_weld_engine")

if (APPLE)
    target_compile_definitions(GridCollider PRIVATE JUCE_PLUGINHOST_AU=1)
else()
    target_compile_definitions(GridCollider PRIVATE JUCE_PLUGINHOST_AU=0)
endif()

if (EXISTS "/Users/user/Documents/weld2/ChucK_weld_engine/third_party/supercollider/include/server/SC_WorldOptions.h")
    target_include_directories(GridCollider
        PRIVATE
            "/Users/user/Documents/weld2/ChucK_weld_engine/third_party/supercollider/include/common"
            "/Users/user/Documents/weld2/ChucK_weld_engine/third_party/supercollider/include/plugin_interface"
            "/Users/user/Documents/weld2/ChucK_weld_engine/third_party/supercollider/include/server")
    target_compile_definitions(GridCollider PRIVATE GRIDCOLLIDER_HAS_SC_HEADERS=1)
endif()

target_link_libraries(GridCollider
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_gui_basics
        juce::juce_gui_extra
        juce::juce_osc
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
