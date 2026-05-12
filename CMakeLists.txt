cmake_minimum_required(VERSION 3.16)
project(virtual_gamepad_linux LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)
pkg_check_modules(IMGUI REQUIRED imgui)
find_package(OpenGL REQUIRED)

set(IMGUI_BACKEND_DIR "/usr/share/doc/libimgui-dev/examples/backends")
if(NOT EXISTS "${IMGUI_BACKEND_DIR}/imgui_impl_sdl2.cpp")
  message(FATAL_ERROR "找不到 Dear ImGui backend 源码，请确认已安装 libimgui-dev")
endif()

add_executable(virtual_gamepad_linux
  src/main.cpp
  src/virtual_gamepad.cpp
  "${IMGUI_BACKEND_DIR}/imgui_impl_sdl2.cpp"
  "${IMGUI_BACKEND_DIR}/imgui_impl_opengl3.cpp"
)

target_include_directories(virtual_gamepad_linux PRIVATE
  src
  ${SDL2_INCLUDE_DIRS}
  ${IMGUI_INCLUDE_DIRS}
  /usr/include/imgui
  /usr/include/imgui/backends
)

target_compile_definitions(virtual_gamepad_linux PRIVATE
  ${SDL2_CFLAGS_OTHER}
  ${IMGUI_CFLAGS_OTHER}
  IMGUI_IMPL_OPENGL_LOADER_GLAD
)

target_link_directories(virtual_gamepad_linux PRIVATE
  ${SDL2_LIBRARY_DIRS}
  ${IMGUI_LIBRARY_DIRS}
)

find_package(X11 REQUIRED)
target_link_libraries(virtual_gamepad_linux PRIVATE
  ${SDL2_LIBRARIES}
  ${IMGUI_LIBRARIES}
  OpenGL::GL
  X11::X11
  X11::Xext
  dl
)

target_compile_options(virtual_gamepad_linux PRIVATE
  -Wall
  -Wextra
  -Wpedantic
)

