cmake_minimum_required(VERSION 3.20)
project(westwood-tools
    VERSION 0.1.0
    LANGUAGES CXX
    DESCRIPTION "Libraries and CLI tools for Westwood game formats"
)

# --------------------------------------------------------------------
# Build options
# --------------------------------------------------------------------

option(WWD_BUILD_SHARED      "Build shared library"           OFF)
option(WWD_BUILD_STATIC      "Build static library"           ON)
option(WWD_BUILD_CLI         "Build command-line tools"       ON)
option(WWD_BUILD_TESTS       "Build tests"                    OFF)
option(WWD_INSTALL           "Generate install target"        ON)
option(WWD_ENABLE_SANITIZERS "Enable ASan/UBSan"              OFF)

# --------------------------------------------------------------------
# Global settings
# --------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(CMAKE_SKIP_BUILD_RPATH FALSE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# --------------------------------------------------------------------
# Compiler settings
# --------------------------------------------------------------------

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
    add_compile_options(-fvisibility=hidden)
    if(WWD_ENABLE_SANITIZERS)
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    endif()
elseif(MSVC)
    add_compile_options(/W4 /WX /utf-8)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    if(WWD_ENABLE_SANITIZERS)
        add_compile_options(/fsanitize=address)
    endif()
endif()

# --------------------------------------------------------------------
# Library sources
# --------------------------------------------------------------------

set(LIBWESTWOOD_SOURCES
    libwestwood/src/error.cpp
    libwestwood/src/io.cpp
    libwestwood/src/lcw.cpp
    libwestwood/src/blowfish.cpp
    libwestwood/src/mix.cpp
    libwestwood/src/vqa.cpp
    libwestwood/src/aud.cpp
    libwestwood/src/shp.cpp
    libwestwood/src/wsa.cpp
    libwestwood/src/tmp.cpp
    libwestwood/src/pal.cpp
    libwestwood/src/fnt.cpp
    libwestwood/src/cps.cpp
    libwestwood/src/png.cpp
)

set(LIBWESTWOOD_HEADERS
    libwestwood/include/westwood/export.h
    libwestwood/include/westwood/error.h
    libwestwood/include/westwood/io.h
    libwestwood/include/westwood/lcw.h
    libwestwood/include/westwood/blowfish.h
    libwestwood/include/westwood/mix.h
    libwestwood/include/westwood/vqa.h
    libwestwood/include/westwood/aud.h
    libwestwood/include/westwood/shp.h
    libwestwood/include/westwood/wsa.h
    libwestwood/include/westwood/tmp.h
    libwestwood/include/westwood/pal.h
    libwestwood/include/westwood/fnt.h
    libwestwood/include/westwood/cps.h
    libwestwood/include/westwood/png.h
    libwestwood/include/westwood/cli.h
)

# --------------------------------------------------------------------
# Static library
# --------------------------------------------------------------------

if(WWD_BUILD_STATIC)
    add_library(westwood_static STATIC ${LIBWESTWOOD_SOURCES})
    target_include_directories(westwood_static PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/libwestwood/include>
        $<INSTALL_INTERFACE:include>
    )
    target_compile_definitions(westwood_static PUBLIC WWD_STATIC)
    set_target_properties(westwood_static PROPERTIES
        OUTPUT_NAME westwood
        PUBLIC_HEADER "${LIBWESTWOOD_HEADERS}"
    )
    add_library(westwood::static ALIAS westwood_static)
endif()

# --------------------------------------------------------------------
# Shared library
# --------------------------------------------------------------------

if(WWD_BUILD_SHARED)
    add_library(westwood_shared SHARED ${LIBWESTWOOD_SOURCES})
    target_include_directories(westwood_shared PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/libwestwood/include>
        $<INSTALL_INTERFACE:include>
    )
    target_compile_definitions(westwood_shared PRIVATE WWD_BUILDING)
    set_target_properties(westwood_shared PROPERTIES
        OUTPUT_NAME westwood
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
        PUBLIC_HEADER "${LIBWESTWOOD_HEADERS}"
    )
    add_library(westwood::shared ALIAS westwood_shared)
endif()

# Default library target
if(WWD_BUILD_STATIC)
    add_library(westwood::westwood ALIAS westwood_static)
elseif(WWD_BUILD_SHARED)
    add_library(westwood::westwood ALIAS westwood_shared)
endif()

# --------------------------------------------------------------------
# CLI tools
# --------------------------------------------------------------------

if(WWD_BUILD_CLI)
    # Helper function for tool creation
    function(add_wwd_tool name)
        add_executable(${name} ${ARGN})
        if(WWD_BUILD_STATIC)
            target_link_libraries(${name} PRIVATE westwood_static)
        elseif(WWD_BUILD_SHARED)
            target_link_libraries(${name} PRIVATE westwood_shared)
        endif()
    endfunction()

    add_wwd_tool(mix-tool mix-tool/main.cpp)
    add_wwd_tool(blowfish-tool blowfish-tool/main.cpp)
    add_wwd_tool(vqa-tool vqa-tool/main.cpp)
    add_wwd_tool(aud-tool aud-tool/main.cpp)
    add_wwd_tool(shp-tool shp-tool/main.cpp)
    add_wwd_tool(wsa-tool wsa-tool/main.cpp)
    add_wwd_tool(tmp-tool tmp-tool/main.cpp)
    add_wwd_tool(pal-tool pal-tool/main.cpp)
    add_wwd_tool(fnt-tool fnt-tool/main.cpp)
    add_wwd_tool(cps-tool cps-tool/main.cpp)
    add_wwd_tool(lcw-tool lcw-tool/main.cpp)
endif()

# --------------------------------------------------------------------
# Installation
# --------------------------------------------------------------------

if(WWD_INSTALL)
    include(GNUInstallDirs)

    if(WWD_BUILD_STATIC)
        install(TARGETS westwood_static
            EXPORT westwood-targets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/westwood
        )
    endif()

    if(WWD_BUILD_SHARED)
        install(TARGETS westwood_shared
            EXPORT westwood-targets
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/westwood
        )
    endif()

    if(WWD_BUILD_CLI)
        install(TARGETS mix-tool blowfish-tool vqa-tool aud-tool shp-tool
                        wsa-tool tmp-tool pal-tool fnt-tool cps-tool lcw-tool
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
    endif()

    install(EXPORT westwood-targets
        FILE westwood-targets.cmake
        NAMESPACE westwood::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/westwood
    )
endif()

# --------------------------------------------------------------------
# Summary
# --------------------------------------------------------------------

message(STATUS "")
message(STATUS "libwestwood ${PROJECT_VERSION} configuration:")
message(STATUS "  Build type:     ${CMAKE_BUILD_TYPE}")
message(STATUS "  Static library: ${WWD_BUILD_STATIC}")
message(STATUS "  Shared library: ${WWD_BUILD_SHARED}")
message(STATUS "  CLI tools:      ${WWD_BUILD_CLI}")
message(STATUS "  Sanitizers:     ${WWD_ENABLE_SANITIZERS}")
message(STATUS "  Install:        ${WWD_INSTALL}")
message(STATUS "")
