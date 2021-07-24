cmake_minimum_required(VERSION 3.20)
project(ward C CXX)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -fdiagnostics-color=always -fsanitize=address,undefined")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fdiagnostics-color=always -O2")

find_package(PkgConfig)
pkg_check_modules(CAIRO REQUIRED IMPORTED_TARGET cairo)

find_package(SDL2 REQUIRED)
# find_package(Cairo REQUIRED)
find_package(X11 REQUIRED)
add_executable(ward main.cpp)
target_link_libraries(ward SDL2::SDL2 SDL2::SDL2main  PkgConfig::CAIRO ${X11_LIBRARIES} ${X11_Xinput_LIB})

install(TARGETS ward DESTINATION bin)

