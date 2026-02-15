cmake_minimum_required(VERSION 3.20)
project(AnalogVUMeter VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

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

qt_add_resources(analog_vu_meter_resources
    resources.qrc
)

option(ANALOGVU_ENABLE_SKIN_IMPORT "Enable AIMP skin ZIP import" ON)

find_path(LIBZIP_INCLUDE_DIR zip.h)
find_library(LIBZIP_LIBRARY NAMES zip)

set(ANALOGVU_HAS_LIBZIP 0)
if(ANALOGVU_ENABLE_SKIN_IMPORT AND LIBZIP_INCLUDE_DIR AND LIBZIP_LIBRARY)
    set(ANALOGVU_HAS_LIBZIP 1)

    add_library(analog_vu_skin_importer STATIC
        src/SkinImporter.cpp
        src/SkinImporter.h
    )

    target_include_directories(analog_vu_skin_importer PRIVATE
        ${LIBZIP_INCLUDE_DIR}
    )

    target_link_libraries(analog_vu_skin_importer PRIVATE
        Qt6::Core
        Qt6::Gui
        ${LIBZIP_LIBRARY}
    )

    target_compile_definitions(analog_vu_skin_importer PRIVATE
        ANALOGVU_HAS_LIBZIP=1
    )
elseif(ANALOGVU_ENABLE_SKIN_IMPORT)
    message(WARNING "libzip not found (zip.h + libzip). Skin import will be disabled at runtime.")
endif()

add_executable(analog_vu_meter
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/SkinManager.cpp
    src/SkinManager.h
    src/StereoVUMeterWidget.cpp
    src/StereoVUMeterWidget.h
    src/AudioCapture.h
    src/VuAudioDsp.cpp
    src/VuAudioDsp.h
    src/VUMeterScale.cpp
    src/VUMeterScale.h
    src/VUMeterSkin.h
    src/VUBallistics.cpp
    src/VUBallistics.h
    ${PLATFORM_SOURCES}
)

target_sources(analog_vu_meter PRIVATE
    ${analog_vu_meter_resources}
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

target_compile_definitions(analog_vu_meter PRIVATE
    ANALOGVU_HAS_LIBZIP=${ANALOGVU_HAS_LIBZIP}
)

if(ANALOGVU_HAS_LIBZIP)
    target_link_libraries(analog_vu_meter PRIVATE
        analog_vu_skin_importer
    )
endif()

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
    
    # Find macdeployqt for bundling Qt frameworks
    get_target_property(_qmake_executable Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qmake_executable}" DIRECTORY)
    find_program(MACDEPLOYQT_EXECUTABLE macdeployqt HINTS "${_qt_bin_dir}")
    
    if(MACDEPLOYQT_EXECUTABLE)
        # Add post-build deployment step to bundle Qt frameworks
        add_custom_command(TARGET analog_vu_meter POST_BUILD
            COMMAND "${MACDEPLOYQT_EXECUTABLE}"
                "$<TARGET_BUNDLE_DIR:analog_vu_meter>"
                -always-overwrite
            COMMENT "Running macdeployqt to bundle Qt frameworks..."
        )
        
        # Re-sign the app bundle after macdeployqt modifies it
        # This fixes "Code Signature Invalid" crashes on macOS
        add_custom_command(TARGET analog_vu_meter POST_BUILD
            COMMAND codesign --force --deep --sign - "$<TARGET_BUNDLE_DIR:analog_vu_meter>"
            COMMENT "Re-signing app bundle after deployment..."
        )
        message(STATUS "macdeployqt found: ${MACDEPLOYQT_EXECUTABLE}")
    else()
        message(WARNING "macdeployqt not found - Qt frameworks will not be bundled automatically")
    endif()
endif()
