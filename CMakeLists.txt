cmake_minimum_required(VERSION 3.16)
project(sysmon CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Curses REQUIRED)

add_executable(sysmon
    src/main.cpp
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
