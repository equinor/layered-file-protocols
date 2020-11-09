cmake_minimum_required(VERSION 3.5)
project(layered-file-protocols LANGUAGES C CXX)

include(CTest)
include(GNUInstallDirs)
include(TestBigEndian)

find_package(fmt REQUIRED)

option(
    LFP_FMT_HEADER_ONLY
    "Use fmtlib in header-only mode"
    FALSE
)
option(
    BUILD_DOC
    "Build documentation"
    FALSE
)
option(
    BUILD_EXAMPLES
    "Build examples"
    FALSE
)

# fmtlib is an imported target, but not marked global, so an ALIAS library
# can't be created, which would be nicer. Fall back to string-resolving the
# namespaced library
if (LFP_FMT_HEADER_ONLY)
    set(fmtlib-comp-def $<TARGET_PROPERTY:fmt::fmt-header-only,INTERFACE_COMPILE_DEFINITIONS>)
    set(fmtlib-incl-dir $<TARGET_PROPERTY:fmt::fmt-header-only,INTERFACE_INCLUDE_DIRECTORIES>)
    set(fmtlib)
else ()
    set(fmtlib-comp-def)
    set(fmtlib-incl-dir)
    set(fmtlib fmt::fmt)
endif ()

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_C_STANDARD 99)
test_big_endian(LFP_BIG_ENDIAN)
if (LFP_BIG_ENDIAN)
    message(STATUS "System is big endian")
else ()
    message(STATUS "System is little endian")
endif ()

if (NOT MSVC)
    # assuming gcc-style options
    # add warnings in debug mode
    list(APPEND lfp-warnings-c++
        -Wall
        -Wextra
        -pedantic
        -Wformat-nonliteral
        -Wcast-align
        -Wpointer-arith
        -Wmissing-declarations
        -Wcast-qual
        -Wwrite-strings
        -Wchar-subscripts
        -Wredundant-decls
    )
endif()

add_library(lfp
    src/lfp.cpp
    src/cfile.cpp
    src/memfile.cpp
    src/tapeimage.cpp
    src/rp66.cpp
)
add_library(lfp::lfp ALIAS lfp)

target_link_libraries(lfp PUBLIC ${fmtlib})

target_include_directories(lfp
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${fmtlib-incl-dir}
)

target_compile_options(lfp
    BEFORE
    PRIVATE
        $<$<CONFIG:Debug>:${lfp-warnings-c++}>
        $<$<CONFIG:RelWithDebInfo>:${lfp-warnings-c++}>
)

target_compile_definitions(lfp
    PRIVATE
        $<$<BOOL:${LFP_BIG_ENDIAN}>:IS_BIG_ENDIAN>
        $<$<NOT:$<BOOL:${LFP_BIG_ENDIAN}>>:IS_LITTLE_ENDIAN>
        ${fmtlib-comp-def}
)

install(
    TARGETS lfp
    EXPORT lfp-export
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/ DESTINATION include)
install(
    EXPORT
        lfp-export
    NAMESPACE
        lfp::
    DESTINATION
        ${CMAKE_INSTALL_DATADIR}/lfp/cmake
    FILE
        lfp-config.cmake
)
export(
    TARGETS
        lfp
    NAMESPACE
        lfp::
    FILE
        lfp-config.cmake
)

if (BUILD_DOC)
    add_subdirectory(docs)
endif ()

if (BUILD_EXAMPLES)
    add_subdirectory(examples)
endif ()

if (NOT BUILD_TESTING)
    return ()
endif ()

if (NOT TARGET Catch2::Catch2)
    add_subdirectory(external/catch2)
endif ()

add_executable(unit-tests
    #test/cfile.cpp
    test/main.cpp
    #test/memfile.cpp
    test/tapeimage.cpp
    #test/rp66.cpp
)
target_compile_definitions(unit-tests
    PRIVATE
        $<$<BOOL:${LFP_BIG_ENDIAN}>:IS_BIG_ENDIAN>
        $<$<NOT:$<BOOL:${LFP_BIG_ENDIAN}>>:IS_LITTLE_ENDIAN>
)
target_link_libraries(unit-tests
    lfp::lfp
    Catch2::Catch2
)
add_test(NAME unit-tests COMMAND unit-tests)
