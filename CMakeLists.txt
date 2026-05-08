cmake_minimum_required(VERSION 3.10)
project(VulkanApp)

set(CMAKE_CXX_STANDARD 17)

find_package(Vulkan REQUIRED)
find_package(SDL2 REQUIRED)
find_package(glm REQUIRED)

set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)
set(IMGUI_BACKENDS_DIR ${IMGUI_DIR}/backends)

add_executable(app
    main.cpp
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_BACKENDS_DIR}/imgui_impl_sdl2.cpp
    ${IMGUI_BACKENDS_DIR}/imgui_impl_sdlrenderer2.cpp
)

target_include_directories(app PRIVATE ${IMGUI_DIR} ${IMGUI_BACKENDS_DIR})

target_link_libraries(app Vulkan::Vulkan SDL2::SDL2 glm::glm)
