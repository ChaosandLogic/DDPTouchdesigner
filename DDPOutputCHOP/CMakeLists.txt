cmake_minimum_required(VERSION 3.10)
project(DDPOutputCHOP)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Source files
set(SOURCES
    DDPOutputCHOP.cpp
    DDPOutputCHOP.h
    CHOP_CPlusPlusBase.h
    CPlusPlus_Common.h
)

# Create MODULE library (plugin)
add_library(DDPOutputCHOP MODULE ${SOURCES})

# Platform-specific settings
if(WIN32)
    # Windows: Link Winsock2
    target_link_libraries(DDPOutputCHOP ws2_32)
    
    # Set output to .dll
    set_target_properties(DDPOutputCHOP PROPERTIES
        PREFIX ""
        SUFFIX ".dll"
    )
    
    # Install to TouchDesigner plugin directory
    # Adjust this path for your system
    set(TD_PLUGIN_DIR "$ENV{USERPROFILE}/Documents/Derivative/Plugins")
    
elseif(APPLE)
    # macOS: Create a proper bundle (like official SDK examples)
    set_target_properties(DDPOutputCHOP PROPERTIES
        PREFIX ""
        BUNDLE TRUE
        BUNDLE_EXTENSION "plugin"
        OSX_ARCHITECTURES "x86_64;arm64"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/Info.plist.template"
    )
    
    # Install to TouchDesigner plugin directory
    # Note: TD 2025 actually uses "TouchDesigner099" directory
    set(TD_PLUGIN_DIR "$ENV{HOME}/Library/Application Support/Derivative/TouchDesigner099/Plugins")
    
    # Install the bundle
    install(TARGETS DDPOutputCHOP
        LIBRARY DESTINATION "${TD_PLUGIN_DIR}"
    )
    
else()
    # Linux
    set_target_properties(DDPOutputCHOP PROPERTIES
        PREFIX ""
        SUFFIX ".so"
    )
    
    set(TD_PLUGIN_DIR "$ENV{HOME}/Documents/Derivative/Plugins")
endif()

# Installation (Windows and Linux only - macOS handled above)
if(NOT APPLE)
    install(TARGETS DDPOutputCHOP
        DESTINATION ${TD_PLUGIN_DIR}
    )
endif()

# Print build info
message(STATUS "Building DDPOutputCHOP for ${CMAKE_SYSTEM_NAME}")
message(STATUS "Install directory: ${TD_PLUGIN_DIR}")

# Build instructions message
add_custom_target(build_info ALL
    COMMAND ${CMAKE_COMMAND} -E echo "=========================================="
    COMMAND ${CMAKE_COMMAND} -E echo "DDP Output CHOP Build Configuration"
    COMMAND ${CMAKE_COMMAND} -E echo "=========================================="
    COMMAND ${CMAKE_COMMAND} -E echo "Platform: ${CMAKE_SYSTEM_NAME}"
    COMMAND ${CMAKE_COMMAND} -E echo "Compiler: ${CMAKE_CXX_COMPILER_ID}"
    COMMAND ${CMAKE_COMMAND} -E echo "Build Type: ${CMAKE_BUILD_TYPE}"
    COMMAND ${CMAKE_COMMAND} -E echo "Install Path: ${TD_PLUGIN_DIR}"
    COMMAND ${CMAKE_COMMAND} -E echo "=========================================="
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "IMPORTANT: Make sure you have downloaded"
    COMMAND ${CMAKE_COMMAND} -E echo "the TouchDesigner SDK headers from:"
    COMMAND ${CMAKE_COMMAND} -E echo "https://github.com/TouchDesigner/CustomOperatorSamples"
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "Required files:"
    COMMAND ${CMAKE_COMMAND} -E echo "  - CHOP_CPlusPlusBase.h"
    COMMAND ${CMAKE_COMMAND} -E echo "  - CPlusPlus_Common.h"
    COMMAND ${CMAKE_COMMAND} -E echo "  - GL_Extensions.h"
    COMMAND ${CMAKE_COMMAND} -E echo "=========================================="
)


