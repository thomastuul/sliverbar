cmake_minimum_required(VERSION 3.20)
project(lemonbar_c VERSION 0.1.0 LANGUAGES C)

include(GNUInstallDirs)
include(CTest)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(XCB QUIET IMPORTED_TARGET xcb)
endif()

option(LEMONBAR_C_SANITIZERS "Enable address and undefined-behavior sanitizers" OFF)

add_library(lemonbar_core
    src/config.c
    src/modules.c
    src/util.c
)
target_include_directories(lemonbar_core PUBLIC include)
target_compile_features(lemonbar_core PUBLIC c_std_17)
target_compile_definitions(lemonbar_core PRIVATE _GNU_SOURCE _POSIX_C_SOURCE=200809L)
target_compile_options(lemonbar_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)

add_executable(lemonbar-panel src/main.c)
target_link_libraries(lemonbar-panel PRIVATE lemonbar_core)
target_compile_definitions(lemonbar-panel PRIVATE _GNU_SOURCE _POSIX_C_SOURCE=200809L)
if(XCB_FOUND)
    target_link_libraries(lemonbar-panel PRIVATE PkgConfig::XCB)
    target_compile_definitions(lemonbar-panel PRIVATE HAVE_XCB=1)
else()
    message(STATUS "XCB development files not found; using the xprop fallback")
endif()
target_compile_options(lemonbar-panel PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)

if(LEMONBAR_C_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    foreach(target lemonbar_core lemonbar-panel test_core)
        if(NOT TARGET ${target})
            continue()
        endif()
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endforeach()
endif()

configure_file(config/panel.conf panel.conf COPYONLY)
install(TARGETS lemonbar-panel RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES config/panel.conf DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/lemonbar-c)

if(BUILD_TESTING)
    add_executable(test_core tests/test_core.c)
    target_link_libraries(test_core PRIVATE lemonbar_core)
    target_compile_options(test_core PRIVATE -Wall -Wextra -Wpedantic)
    if(LEMONBAR_C_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(test_core PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(test_core PRIVATE -fsanitize=address,undefined)
    endif()
    add_test(NAME core COMMAND test_core)
    add_test(NAME check_config COMMAND lemonbar-panel --config ${CMAKE_CURRENT_BINARY_DIR}/panel.conf --check-config)
    add_test(NAME version COMMAND lemonbar-panel --version)
endif()
