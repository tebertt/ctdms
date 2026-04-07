cmake_minimum_required(VERSION 3.14)

project(ctdms
    VERSION 0.1.0
    DESCRIPTION "C library for reading NI TDMS files"
    LANGUAGES C
)

# Options
option(CTDMS_BUILD_SHARED "Build shared library" OFF)
option(CTDMS_BUILD_TESTS "Build tests" ON)
option(CTDMS_BUILD_EXAMPLES "Build examples" ON)

# C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Sources
set(CTDMS_SOURCES
    src/ctdms.c
    src/ctdms_parse.c
    src/ctdms_io.c
)

# Library target
if(CTDMS_BUILD_SHARED)
    add_library(ctdms SHARED ${CTDMS_SOURCES})
    target_compile_definitions(ctdms
        PRIVATE CTDMS_BUILDING
        PUBLIC CTDMS_SHARED
    )
else()
    add_library(ctdms STATIC ${CTDMS_SOURCES})
endif()

target_include_directories(ctdms
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Compiler warnings
if(MSVC)
    target_compile_options(ctdms PRIVATE /W4)
else()
    target_compile_options(ctdms PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Platform-specific
if(WIN32)
    # Nothing extra needed
elseif(UNIX)
    target_link_libraries(ctdms PRIVATE m)
endif()

# Tests
if(CTDMS_BUILD_TESTS)
    enable_testing()
    add_executable(test_ctdms tests/test_ctdms.c)
    target_link_libraries(test_ctdms PRIVATE ctdms)
    target_include_directories(test_ctdms PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME test_ctdms COMMAND test_ctdms)
endif()

# Examples
if(CTDMS_BUILD_EXAMPLES)
    add_executable(example_read examples/example_read.c)
    target_link_libraries(example_read PRIVATE ctdms)
endif()

# Install
include(GNUInstallDirs)

install(TARGETS ctdms
    EXPORT ctdmsTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/ctdms
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT ctdmsTargets
    FILE ctdmsConfig.cmake
    NAMESPACE ctdms::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ctdms
)
