cmake_minimum_required(VERSION 3.10)
project(VulkanApp)

set(CMAKE_CXX_STANDARD 17)

include(FetchContent)

find_package(Vulkan REQUIRED)
find_package(SDL2 REQUIRED)
find_package(glm REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil libswscale libswresample)

# RtMidi for MIDI support
FetchContent_Declare(
    rtmidi
    GIT_REPOSITORY https://github.com/thestk/rtmidi.git
    GIT_TAG 5.0.0
)
FetchContent_MakeAvailable(rtmidi)

set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)
set(IMGUI_BACKENDS_DIR ${IMGUI_DIR}/backends)

add_executable(app
    src/main.cpp
    src/app/Application.cpp
    src/app/UISystem.cpp
    src/app/ControlState.cpp
    src/app/MidiSystem.cpp
    src/core/Window.cpp
    src/core/VulkanContext.cpp
    src/video/VideoStaging.cpp
    src/video/VideoPlayer.cpp
    src/video/VideoRegistry.cpp
    src/video/VideoTexture.cpp
    src/video/VideoRenderer.cpp
    src/video/CpuFramePool.h
    src/video/StagingRing.h
    src/render/MultiPassPipeline.cpp
    src/render/ShaderCompiler.cpp
    src/render/UniformBufferManager.cpp
    src/render/DescriptorSetManager.cpp
    src/gfx/FrameSystem.cpp
    src/gfx/MemoryAllocator.cpp
    src/gfx/ResourceSystem.cpp
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_BACKENDS_DIR}/imgui_impl_sdl2.cpp
    ${IMGUI_BACKENDS_DIR}/imgui_impl_sdlrenderer2.cpp
)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${IMGUI_DIR} ${IMGUI_BACKENDS_DIR})

target_link_libraries(app
    Vulkan::Vulkan
    SDL2::SDL2
    glm::glm
    PkgConfig::FFMPEG
    rtmidi
)
