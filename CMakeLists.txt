cmake_minimum_required(VERSION 3.16)
project(sysmon CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(sysmon src/main.cpp src/stats_posix.cpp)
if(APPLE)
    target_sources(sysmon PRIVATE src/stats_macos.cpp)
endif()
