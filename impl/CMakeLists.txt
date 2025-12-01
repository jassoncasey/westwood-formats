cmake_minimum_required(VERSION 3.20)
project(mix-tool
    VERSION 0.1.0
    LANGUAGES CXX
    DESCRIPTION "Library and CLI for Westwood MIX archive files"
)

# --------------------------------------------------------------------
# Build options
# --------------------------------------------------------------------

option(MIX_BUILD_SHARED   "Build shared library"           OFF)
option(MIX_BUILD_STATIC   "Build static library"           ON)
option(MIX_BUILD_CLI      "Build command-line tool"        ON)
option(MIX_BUILD_TESTS    "Build tests"                    OFF)
option(MIX_INSTALL        "Generate install target"        ON)

# --------------------------------------------------------------------
# Global settings
# --------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Position independent code (required for shared lib, good practice for static)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# RPATH settings for installed binaries
set(CMAKE_SKIP_BUILD_RPATH FALSE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

# Default to Release if not specified
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# --------------------------------------------------------------------
# Compiler settings
# --------------------------------------------------------------------

# Warning flags
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
    # Hide symbols by default (expose only MIX_API marked functions)
    add_compile_options(-fvisibility=hidden)
elseif(MSVC)
    add_compile_options(/W4 /WX)
    # UTF-8 source and execution charset
    add_compile_options(/utf-8)
    # Disable warnings about deprecated POSIX names
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

# --------------------------------------------------------------------
# Library sources
# --------------------------------------------------------------------

set(LIBMIX_SOURCES
    libmix/src/error.cpp
    libmix/src/hash.cpp
    libmix/src/mix_reader.cpp
    libmix/src/mix_c.cpp
)

set(LIBMIX_HEADERS
    libmix/include/mix/export.h
    libmix/include/mix/types.h
    libmix/include/mix/error.h
    libmix/include/mix/mix.h
    libmix/include/mix/mix_c.h
)

# --------------------------------------------------------------------
# Static library
# --------------------------------------------------------------------

if(MIX_BUILD_STATIC)
    add_library(mix_static STATIC ${LIBMIX_SOURCES})
    target_include_directories(mix_static PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/libmix/include>
        $<INSTALL_INTERFACE:include>
    )
    target_compile_definitions(mix_static PUBLIC MIX_STATIC)
    set_target_properties(mix_static PROPERTIES
        OUTPUT_NAME mix
        PUBLIC_HEADER "${LIBMIX_HEADERS}"
    )
    # Alias for use in subdirectories
    add_library(mix::static ALIAS mix_static)
endif()

# --------------------------------------------------------------------
# Shared library
# --------------------------------------------------------------------

if(MIX_BUILD_SHARED)
    add_library(mix_shared SHARED ${LIBMIX_SOURCES})
    target_include_directories(mix_shared PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/libmix/include>
        $<INSTALL_INTERFACE:include>
    )
    target_compile_definitions(mix_shared PRIVATE MIX_BUILDING)
    set_target_properties(mix_shared PROPERTIES
        OUTPUT_NAME mix
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
        PUBLIC_HEADER "${LIBMIX_HEADERS}"
    )
    # Alias for use in subdirectories
    add_library(mix::shared ALIAS mix_shared)
endif()

# Default library target (prefer static)
if(MIX_BUILD_STATIC)
    add_library(mix::mix ALIAS mix_static)
elseif(MIX_BUILD_SHARED)
    add_library(mix::mix ALIAS mix_shared)
endif()

# --------------------------------------------------------------------
# CLI tool
# --------------------------------------------------------------------

if(MIX_BUILD_CLI)
    add_executable(mix-tool
        mix-tool/main.cpp
        mix-tool/cmd_list.cpp
        mix-tool/format.cpp
    )
    # Link against static library if available, else shared
    if(MIX_BUILD_STATIC)
        target_link_libraries(mix-tool PRIVATE mix_static)
    elseif(MIX_BUILD_SHARED)
        target_link_libraries(mix-tool PRIVATE mix_shared)
    endif()
endif()

# --------------------------------------------------------------------
# Installation
# --------------------------------------------------------------------

if(MIX_INSTALL)
    include(GNUInstallDirs)

    # Install libraries
    if(MIX_BUILD_STATIC)
        install(TARGETS mix_static
            EXPORT mix-targets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/mix
        )
    endif()

    if(MIX_BUILD_SHARED)
        install(TARGETS mix_shared
            EXPORT mix-targets
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}  # Windows DLLs
            PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/mix
        )
    endif()

    # Install CLI tool
    if(MIX_BUILD_CLI)
        install(TARGETS mix-tool
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
    endif()

    # Export targets for find_package() support
    install(EXPORT mix-targets
        FILE mix-targets.cmake
        NAMESPACE mix::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mix
    )

    # Package config file
    include(CMakePackageConfigHelpers)

    configure_package_config_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/mix-config.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/mix-config.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mix
    )

    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/mix-config-version.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )

    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/mix-config.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/mix-config-version.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mix
    )
endif()

# --------------------------------------------------------------------
# Summary
# --------------------------------------------------------------------

message(STATUS "")
message(STATUS "libmix ${PROJECT_VERSION} configuration:")
message(STATUS "  Build type:     ${CMAKE_BUILD_TYPE}")
message(STATUS "  Static library: ${MIX_BUILD_STATIC}")
message(STATUS "  Shared library: ${MIX_BUILD_SHARED}")
message(STATUS "  CLI tool:       ${MIX_BUILD_CLI}")
message(STATUS "  Install:        ${MIX_INSTALL}")
message(STATUS "")
