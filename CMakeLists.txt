cmake_minimum_required(VERSION 3.22)
project(Sonora VERSION 0.15.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.6
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_gui_app(Sonora PRODUCT_NAME "Sonora" COMPANY_NAME "Sonora"
    BUNDLE_ID "org.sonora.Sonora")
target_sources(Sonora PRIVATE src/Main.cpp src/MainComponent.cpp src/AudioEngine.cpp
    src/PianoRoll.cpp src/ProjectIO.cpp src/DrumSampler.cpp src/DrumSequencer.cpp src/Export.cpp src/Fx.cpp src/AudioTakes.cpp src/PitchCorrect.cpp src/KitSamples.cpp)
target_compile_definitions(Sonora PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_JACK=1
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Sonora,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(Sonora PRIVATE juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
install(TARGETS Sonora RUNTIME DESTINATION bin)
install(FILES packaging/sonora.desktop DESTINATION share/applications)
install(FILES assets/sonora.svg DESTINATION share/icons/hicolor/scalable/apps)
install(FILES packaging/org.sonora.Sonora.metainfo.xml DESTINATION share/metainfo)
install(FILES LICENSE README.md DESTINATION share/doc/sonora)

include(CTest)
if(BUILD_TESTING)
    juce_add_console_app(SonoraTests PRODUCT_NAME "Sonora Core Tests")
    target_sources(SonoraTests PRIVATE tests/CoreTests.cpp src/ProjectIO.cpp src/AudioEngine.cpp src/DrumSampler.cpp src/Export.cpp src/Fx.cpp src/AudioTakes.cpp src/PitchCorrect.cpp src/KitSamples.cpp)
    target_include_directories(SonoraTests PRIVATE src)
    target_compile_definitions(SonoraTests PRIVATE JUCE_USE_CURL=0 JUCE_JACK=1)
    target_link_libraries(SonoraTests PRIVATE juce::juce_audio_devices juce::juce_audio_formats
        juce::juce_recommended_config_flags)
    add_test(NAME sonora_core COMMAND SonoraTests)
    set_tests_properties(sonora_core PROPERTIES TIMEOUT 120)
endif()
