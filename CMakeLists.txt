cmake_minimum_required(VERSION 3.10)
project(launcher C CXX)

include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/imgui)
include_directories(${CMAKE_SOURCE_DIR}/imgui/backends)
include_directories(${CMAKE_SOURCE_DIR}/src)

file(GLOB SRC_FILES src/*.cpp src/*.c)
file(GLOB IMGUI ${CMAKE_SOURCE_DIR}/imgui/*.cpp ${CMAKE_SOURCE_DIR}/imgui/backends/*.cpp)
add_executable(launcher WIN32 ${SRC_FILES} ${IMGUI})

target_link_libraries(launcher "${CMAKE_SOURCE_DIR}/lib/libglfw3.a" opengl32)