cmake_minimum_required(VERSION 3.10)
project(DDPInputCHOP)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Source files
set(SOURCE_FILES
    DDPInputCHOP.cpp
    DDPInputCHOP.h
    CHOP_CPlusPlusBase.h
    CPlusPlus_Common.h
)

# Platform-specific settings
if(APPLE)
    # macOS - create bundle
    add_library(DDPInputCHOP MODULE ${SOURCE_FILES})
    
    set_target_properties(DDPInputCHOP PROPERTIES
        BUNDLE TRUE
        BUNDLE_EXTENSION "plugin"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/Info.plist.template"
    )
    
    # Install location
    set(TD_PLUGIN_DIR "$ENV{HOME}/Library/Application Support/Derivative/TouchDesigner099/Plugins")
    
    install(TARGETS DDPInputCHOP
        LIBRARY DESTINATION "${TD_PLUGIN_DIR}"
    )
    
elseif(WIN32)
    # Windows - DLL
    add_library(DDPInputCHOP SHARED ${SOURCE_FILES})
    
    set_target_properties(DDPInputCHOP PROPERTIES
        PREFIX ""
        SUFFIX ".dll"
    )
    
    # Install location
    set(TD_PLUGIN_DIR "$ENV{USERPROFILE}/Documents/Derivative/Plugins")
    
    install(TARGETS DDPInputCHOP
        RUNTIME DESTINATION "${TD_PLUGIN_DIR}"
    )
    
else()
    # Linux - SO
    add_library(DDPInputCHOP SHARED ${SOURCE_FILES})
    
    set_target_properties(DDPInputCHOP PROPERTIES
        PREFIX ""
        SUFFIX ".so"
    )
    
    # Install location
    set(TD_PLUGIN_DIR "$ENV{HOME}/Documents/Derivative/Plugins")
    
    install(TARGETS DDPInputCHOP
        LIBRARY DESTINATION "${TD_PLUGIN_DIR}"
    )
endif()

# Print configuration
add_custom_target(build_info ALL
    COMMAND ${CMAKE_COMMAND} -E echo "=========================================="
    COMMAND ${CMAKE_COMMAND} -E echo "DDP Input CHOP Build Configuration"
    COMMAND ${CMAKE_COMMAND} -E echo "=========================================="
    COMMAND ${CMAKE_COMMAND} -E echo "Platform: ${CMAKE_SYSTEM_NAME}"
    COMMAND ${CMAKE_COMMAND} -E echo "Compiler: ${CMAKE_CXX_COMPILER_ID}"
    COMMAND ${CMAKE_COMMAND} -E echo "Build Type: ${CMAKE_BUILD_TYPE}"
    COMMAND ${CMAKE_COMMAND} -E echo "Install Path: ${TD_PLUGIN_DIR}"
    COMMAND ${CMAKE_COMMAND} -E echo "=========================================="
)



