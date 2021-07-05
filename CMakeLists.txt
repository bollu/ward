cmake_minimum_required(VERSION 3.20)
project(ward C CXX)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "-fdiagnostics-color=always -O2 -g -Wall -fsanitize=address -fsanitize=undefined")

find_package(SDL2)
find_package(X11)
add_executable(ward main.cpp)
target_link_libraries(ward SDL2::SDL2 SDL2::SDL2main  ${X11_LIBRARIES} ${X11_Xinput_LIB})

