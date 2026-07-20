cmake_minimum_required(VERSION 3.20)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" SLIVERBAR_VERSION LIMIT_COUNT 1)
if(NOT SLIVERBAR_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "VERSION must contain one Semantic Versioning number")
endif()

project(sliverbar VERSION "${SLIVERBAR_VERSION}" LANGUAGES C)

include(GNUInstallDirs)
include(CTest)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(XCB QUIET IMPORTED_TARGET xcb)
    pkg_check_modules(CAIRO_XCB QUIET IMPORTED_TARGET cairo-xcb)
    pkg_check_modules(PANGOCAIRO QUIET IMPORTED_TARGET pangocairo)
endif()

option(SLIVERBAR_SANITIZERS "Enable address and undefined-behavior sanitizers" OFF)
option(SLIVERBAR_WITH_XCB "Build the native XCB event backend when available" ON)

add_library(sliverbar_core
    src/config.c
    src/modules.c
    src/util.c
)
target_include_directories(sliverbar_core PUBLIC include)
target_compile_features(sliverbar_core PUBLIC c_std_17)
target_compile_definitions(sliverbar_core PRIVATE _GNU_SOURCE _POSIX_C_SOURCE=200809L)
target_compile_options(sliverbar_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)

add_executable(sliverbar src/main.c)
target_link_libraries(sliverbar PRIVATE sliverbar_core)
target_compile_definitions(sliverbar PRIVATE _GNU_SOURCE _POSIX_C_SOURCE=200809L)
configure_file(include/version.h.in generated/version.h @ONLY)
target_include_directories(sliverbar PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
if(SLIVERBAR_WITH_XCB AND XCB_FOUND AND CAIRO_XCB_FOUND AND PANGOCAIRO_FOUND)
    target_sources(sliverbar PRIVATE src/native_panel.c src/native_tray.c)
    target_link_libraries(
        sliverbar PRIVATE PkgConfig::XCB PkgConfig::CAIRO_XCB PkgConfig::PANGOCAIRO
    )
    target_compile_definitions(sliverbar PRIVATE HAVE_XCB=1 HAVE_NATIVE_PANEL=1)
    set(SLIVERBAR_NATIVE_PANEL ON)
else()
    message(STATUS "Native X11 dependencies unavailable or disabled; building CLI-only fallback")
endif()
target_compile_options(sliverbar PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)

if(SLIVERBAR_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    foreach(target sliverbar_core sliverbar test_core)
        if(NOT TARGET ${target})
            continue()
        endif()
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endforeach()
endif()

configure_file(config/panel.conf panel.conf COPYONLY)
install(TARGETS sliverbar RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES config/panel.conf DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/sliverbar)

if(BUILD_TESTING)
    add_executable(test_core tests/test_core.c)
    target_link_libraries(test_core PRIVATE sliverbar_core)
    target_compile_options(test_core PRIVATE -Wall -Wextra -Wpedantic)
    if(SLIVERBAR_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(test_core PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(test_core PRIVATE -fsanitize=address,undefined)
    endif()
    add_test(NAME core COMMAND test_core)
    add_test(NAME check_config COMMAND sliverbar --config ${CMAKE_CURRENT_BINARY_DIR}/panel.conf --check-config)
    add_test(NAME version COMMAND sliverbar --version)
    set_tests_properties(version PROPERTIES
        PASS_REGULAR_EXPRESSION "^sliverbar ${PROJECT_VERSION}"
    )
    if(SLIVERBAR_NATIVE_PANEL)
        find_program(XVFB_RUN xvfb-run)
        if(XVFB_RUN)
            add_test(
                NAME native_panel_smoke
                COMMAND ${XVFB_RUN} -a $<TARGET_FILE:sliverbar>
                        --config ${CMAKE_CURRENT_BINARY_DIR}/panel.conf --smoke-test
            )
        endif()
    endif()
endif()
