cmake_minimum_required(VERSION 3.16)
project(launcher LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(APP_SOURCES
    src/main.cpp
    src/ui.cpp
    src/font.cpp
    src/app.cpp
    src/config.cpp
    src/util.cpp
    src/sha1.cpp
    src/http.cpp
    src/java_finder.cpp
    src/mc_version.cpp
    src/mc_launch.cpp
    src/mc_download.cpp
)

set(IMGUI_SOURCES
    imgui/imgui.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp
    imgui/backends/imgui_impl_glfw.cpp
    imgui/backends/imgui_impl_opengl3.cpp
)

add_executable(launcher WIN32 ${APP_SOURCES} ${IMGUI_SOURCES})

target_include_directories(launcher PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/imgui
    ${CMAKE_SOURCE_DIR}/imgui/backends
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party
)

target_compile_definitions(launcher PRIVATE
    IMGUI_DEFINE_MATH_OPERATORS
)

if(MSVC)
    target_compile_options(launcher PRIVATE /utf-8 /W3)
else()
    target_compile_options(launcher PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# Bundled static GLFW (MinGW .a). For MSVC, install GLFW and switch link line.
set(LAUNCHER_LIBS
    opengl32
    gdi32
    shell32
    ole32
    winhttp
    bcrypt
)

if(MINGW OR (WIN32 AND EXISTS "${CMAKE_SOURCE_DIR}/lib/libglfw3.a"))
    target_link_libraries(launcher PRIVATE
        "${CMAKE_SOURCE_DIR}/lib/libglfw3.a"
        ${LAUNCHER_LIBS}
    )
else()
    find_package(glfw3 CONFIG QUIET)
    if(TARGET glfw)
        target_link_libraries(launcher PRIVATE glfw ${LAUNCHER_LIBS})
    else()
        target_link_libraries(launcher PRIVATE
            "${CMAKE_SOURCE_DIR}/lib/libglfw3.a"
            ${LAUNCHER_LIBS}
        )
    endif()
endif()
