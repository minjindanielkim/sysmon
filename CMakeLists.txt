cmake_minimum_required(VERSION 3.16)
project(sysmon CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Optimised build unless asked otherwise (-DCMAKE_BUILD_TYPE=Debug)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(Curses REQUIRED)

add_executable(sysmon
    src/main.cpp
    src/options.cpp
    src/sampler.cpp
    src/ui.cpp
    src/stats_posix.cpp
)
if(APPLE)
    target_sources(sysmon PRIVATE src/stats_macos.cpp)
endif()

target_include_directories(sysmon PRIVATE ${CURSES_INCLUDE_DIRS})
target_link_libraries(sysmon PRIVATE ${CURSES_LIBRARIES})
target_compile_options(sysmon PRIVATE -Wall -Wextra)

# `cmake --install build` copies the binary to <prefix>/bin
# (prefix defaults to /usr/local; override with --prefix ~/.local)
include(GNUInstallDirs)
install(TARGETS sysmon RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
