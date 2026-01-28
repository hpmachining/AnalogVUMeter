cmake_minimum_required(VERSION 3.20)
project(AnalogVUMeterQt VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC OFF)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

# Generate version config header
configure_file(
    ${CMAKE_SOURCE_DIR}/src/version.h.in
    ${CMAKE_BINARY_DIR}/version.h
    @ONLY
)

# Platform-specific sources and dependencies
if(APPLE)
    set(PLATFORM_SOURCES
        src/AudioCapture_macos.cpp
    )
    
    # Find CoreAudio and AudioToolbox frameworks
    find_library(COREAUDIO_FRAMEWORK CoreAudio REQUIRED)
    find_library(AUDIOTOOLBOX_FRAMEWORK AudioToolbox REQUIRED)
    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
    
    set(PLATFORM_LIBRARIES
        ${COREAUDIO_FRAMEWORK}
        ${AUDIOTOOLBOX_FRAMEWORK}
        ${COREFOUNDATION_FRAMEWORK}
    )
    set(PLATFORM_INCLUDE_DIRS "")
    set(PLATFORM_COMPILE_OPTIONS "")
else()
    # Linux - use PulseAudio
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(PULSEAUDIO REQUIRED libpulse)
    
    set(PLATFORM_SOURCES
        src/AudioCapture_linux.cpp
    )
    set(PLATFORM_LIBRARIES ${PULSEAUDIO_LIBRARIES})
    set(PLATFORM_INCLUDE_DIRS ${PULSEAUDIO_INCLUDE_DIRS})
    set(PLATFORM_COMPILE_OPTIONS ${PULSEAUDIO_CFLAGS_OTHER})
endif()

add_executable(analog_vu_meter
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/StereoVUMeterWidget.cpp
    src/StereoVUMeterWidget.h
    src/AudioCapture.h
    src/VUBallistics.cpp
    src/VUBallistics.h
    ${PLATFORM_SOURCES}
)

# Include directories
target_include_directories(analog_vu_meter PRIVATE
    ${CMAKE_BINARY_DIR}
    ${PLATFORM_INCLUDE_DIRS}
)

# Link Qt and platform-specific libraries
target_link_libraries(analog_vu_meter PRIVATE
    Qt6::Widgets
    ${PLATFORM_LIBRARIES}
)

# Platform-specific compiler flags
if(PLATFORM_COMPILE_OPTIONS)
    target_compile_options(analog_vu_meter PRIVATE ${PLATFORM_COMPILE_OPTIONS})
endif()

# macOS-specific settings
if(APPLE)
    # Configure Info.plist from template
    configure_file(
        ${CMAKE_SOURCE_DIR}/Info.plist.in
        ${CMAKE_BINARY_DIR}/Info.plist
        @ONLY
    )
    
    # Set the app icon as a resource
    set(APP_ICON_MACOS ${CMAKE_SOURCE_DIR}/resources/AppIcon.icns)
    set_source_files_properties(${APP_ICON_MACOS} PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources"
    )
    
    # Add icon to the executable sources
    target_sources(analog_vu_meter PRIVATE ${APP_ICON_MACOS})
    
    set_target_properties(analog_vu_meter PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_BINARY_DIR}/Info.plist
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.github.analog-vu-meter"
        MACOSX_BUNDLE_BUNDLE_NAME "Analog VU Meter"
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE_ICON_FILE "AppIcon"
    )
    
    # Enable ARC (Automatic Reference Counting) for Objective-C++ if needed
    # target_compile_options(analog_vu_meter PRIVATE -fobjc-arc)
endif()
