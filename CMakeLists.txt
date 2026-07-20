cmake_minimum_required(VERSION 3.20)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" SLIVERBAR_VERSION LIMIT_COUNT 1)
if(NOT SLIVERBAR_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "VERSION must contain one Semantic Versioning number")
endif()

project(sliverbar VERSION "${SLIVERBAR_VERSION}" LANGUAGES C)

include(GNUInstallDirs)
include(CTest)
set(SLIVERBAR_SYSTEM_CONFIG_DIR "/etc/sliverbar")
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(XCB QUIET IMPORTED_TARGET xcb)
    pkg_check_modules(CAIRO_XCB QUIET IMPORTED_TARGET cairo-xcb)
    pkg_check_modules(PANGOCAIRO QUIET IMPORTED_TARGET pangocairo)
    pkg_check_modules(GIO QUIET IMPORTED_TARGET gio-2.0 gio-unix-2.0)
    pkg_check_modules(XKBCOMMON_X11 QUIET IMPORTED_TARGET xkbcommon-x11)
    pkg_check_modules(XCB_RANDR QUIET IMPORTED_TARGET xcb-randr)
endif()

option(SLIVERBAR_SANITIZERS "Enable address and undefined-behavior sanitizers" OFF)
option(SLIVERBAR_WITH_XCB "Build the native XCB event backend when available" ON)

add_library(sliverbar_core
    src/app_launcher.c
    src/config.c
    src/inhibitor.c
    src/modules.c
    src/power_actions.c
    src/util.c
)
target_include_directories(sliverbar_core PUBLIC include)
target_compile_features(sliverbar_core PUBLIC c_std_17)
target_compile_definitions(sliverbar_core PRIVATE _GNU_SOURCE _POSIX_C_SOURCE=200809L)
target_compile_options(sliverbar_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
if(GIO_FOUND)
    target_link_libraries(sliverbar_core PRIVATE PkgConfig::GIO)
    target_compile_definitions(sliverbar_core PRIVATE HAVE_GIO=1)
endif()

add_executable(sliverbar src/main.c)
target_link_libraries(sliverbar PRIVATE sliverbar_core)
target_compile_definitions(sliverbar PRIVATE _GNU_SOURCE _POSIX_C_SOURCE=200809L)
target_compile_definitions(
    sliverbar PRIVATE
    SLIVERBAR_SYSTEM_CONFIG="${SLIVERBAR_SYSTEM_CONFIG_DIR}/panel.conf"
)
configure_file(include/version.h.in generated/version.h @ONLY)
target_include_directories(sliverbar PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
if(SLIVERBAR_WITH_XCB AND XCB_FOUND AND CAIRO_XCB_FOUND AND PANGOCAIRO_FOUND)
    target_sources(
        sliverbar PRIVATE
        src/native_panel.c
        src/native_popup.c
        src/native_tray.c
        src/workspace_backend.c
    )
    target_link_libraries(
        sliverbar PRIVATE PkgConfig::XCB PkgConfig::CAIRO_XCB PkgConfig::PANGOCAIRO
    )
    target_compile_definitions(sliverbar PRIVATE HAVE_XCB=1 HAVE_NATIVE_PANEL=1)
    if(XKBCOMMON_X11_FOUND)
        target_link_libraries(sliverbar PRIVATE PkgConfig::XKBCOMMON_X11)
        target_compile_definitions(sliverbar PRIVATE HAVE_XKBCOMMON_X11=1)
    endif()
    if(XCB_RANDR_FOUND)
        target_link_libraries(sliverbar PRIVATE PkgConfig::XCB_RANDR)
        target_compile_definitions(sliverbar PRIVATE HAVE_XCB_RANDR=1)
    endif()
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
file(READ config/panel.conf SLIVERBAR_TEST_CONFIG)
string(REPLACE "monitor=primary" "monitor=RIGHT"
       SLIVERBAR_MULTI_MONITOR_CONFIG "${SLIVERBAR_TEST_CONFIG}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/panel-multi-monitor.conf"
     "${SLIVERBAR_MULTI_MONITOR_CONFIG}")
install(TARGETS sliverbar RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES config/panel.conf DESTINATION ${SLIVERBAR_SYSTEM_CONFIG_DIR})

set(CPACK_PACKAGE_NAME "sliverbar")
set(CPACK_PACKAGE_VENDOR "Sliverbar contributors")
set(CPACK_PACKAGE_CONTACT "Sliverbar contributors")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Lightweight native X11 panel with EWMH and optional bspwm support")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_SET_DESTDIR ON)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SECTION "x11")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "shared-mime-info")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/conffiles"
     "/etc/sliverbar/panel.conf\n")
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
    "${CMAKE_CURRENT_BINARY_DIR}/conffiles")
include(CPack)

if(BUILD_TESTING)
    add_executable(test_core tests/test_core.c)
    target_link_libraries(test_core PRIVATE sliverbar_core)
    target_compile_options(test_core PRIVATE -Wall -Wextra -Wpedantic)
    if(GIO_FOUND)
        add_executable(test_app_defaults tests/test_app_defaults.c)
        target_link_libraries(test_app_defaults PRIVATE sliverbar_core)
        target_compile_options(test_app_defaults PRIVATE -Wall -Wextra -Wpedantic)
        if(SLIVERBAR_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(test_app_defaults PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(test_app_defaults PRIVATE -fsanitize=address,undefined)
        endif()
        add_test(NAME app_defaults COMMAND test_app_defaults)
        set_tests_properties(app_defaults PROPERTIES SKIP_RETURN_CODE 77)
    endif()
    if(SLIVERBAR_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(test_core PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(test_core PRIVATE -fsanitize=address,undefined)
    endif()
    add_test(NAME core COMMAND test_core)
    add_test(NAME check_config COMMAND sliverbar --config ${CMAKE_CURRENT_BINARY_DIR}/panel.conf --check-config)
    add_test(NAME version COMMAND sliverbar --version)
    add_test(NAME diagnose COMMAND sliverbar --config ${CMAKE_CURRENT_BINARY_DIR}/panel.conf --diagnose)
    add_test(
        NAME config_search
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/config-search.sh
                $<TARGET_FILE:sliverbar>
    )
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
            add_test(
                NAME native_multi_monitor
                COMMAND ${XVFB_RUN} -a ${CMAKE_CURRENT_SOURCE_DIR}/tests/xvfb-multi-monitor.sh
                        $<TARGET_FILE:sliverbar>
                        ${CMAKE_CURRENT_BINARY_DIR}/panel-multi-monitor.conf
            )
            add_test(
                NAME native_second_xscreen
                COMMAND ${XVFB_RUN} -a -s "-screen 0 800x600x24 -screen 1 1024x768x24"
                        ${CMAKE_CURRENT_SOURCE_DIR}/tests/xvfb-second-screen.sh
                        $<TARGET_FILE:sliverbar>
                        ${CMAKE_CURRENT_BINARY_DIR}/panel.conf
            )
            add_test(
                NAME native_workspace_backends
                COMMAND ${XVFB_RUN} -a
                        ${CMAKE_CURRENT_SOURCE_DIR}/tests/xvfb-workspace-backends.sh
                        $<TARGET_FILE:sliverbar>
                        ${CMAKE_CURRENT_BINARY_DIR}/panel.conf
            )
        endif()
    endif()
endif()
