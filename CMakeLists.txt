cmake_minimum_required(VERSION 3.10)
project(ProjektSO1 CXX)

set(CMAKE_CXX_STANDARD 17)

find_package(Curses REQUIRED)
find_package(Threads REQUIRED)

add_executable(ProjektSO1 main.cpp)

target_link_libraries(ProjektSO1 PRIVATE ${CURSES_LIBRARIES} Threads::Threads)