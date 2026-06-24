cmake_minimum_required(VERSION 3.10)

project(VulkanApp)

set(CMAKE_CXX_STANDARD 20)

include(FetchContent)

find_package(Vulkan REQUIRED)
find_package(SDL2 REQUIRED)
find_package(glm REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil libswscale libswresample)

# --- Dependency helpers: prefer local third_party/ copies for offline builds ---
set(RTMIDI_LOCAL ${CMAKE_CURRENT_SOURCE_DIR}/third_party/rtmidi)
set(KFR_LOCAL    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/kfr)
set(NLOHMANN_JSON_LOCAL ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann_json)

# RtMidi for MIDI support
if(EXISTS ${RTMIDI_LOCAL}/CMakeLists.txt)
    message(STATUS "Using local rtmidi from ${RTMIDI_LOCAL}")
    set(rtmidi_SOURCE_DIR ${RTMIDI_LOCAL})
    add_subdirectory(${RTMIDI_LOCAL} ${CMAKE_BINARY_DIR}/rtmidi-build)
else()
    FetchContent_Declare(
        rtmidi
        GIT_REPOSITORY https://github.com/thestk/rtmidi.git
        GIT_TAG 5.0.0
        UPDATE_DISCONNECTED TRUE
    )
    FetchContent_MakeAvailable(rtmidi)
endif()

# KFR for DSP/audio processing
if(EXISTS ${KFR_LOCAL}/CMakeLists.txt)
    message(STATUS "Using local kfr from ${KFR_LOCAL}")
    set(KFR_IO OFF CACHE BOOL "" FORCE)
    set(kfr_SOURCE_DIR ${KFR_LOCAL})
    add_subdirectory(${KFR_LOCAL} ${CMAKE_BINARY_DIR}/kfr-build)
else()
    set(KFR_IO OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        kfr
        GIT_REPOSITORY https://github.com/kfrlib/kfr.git
        GIT_TAG main
        UPDATE_DISCONNECTED TRUE
    )
    FetchContent_MakeAvailable(kfr)
endif()

# nlohmann/json for parameter serialization
if(EXISTS ${NLOHMANN_JSON_LOCAL}/CMakeLists.txt)
    message(STATUS "Using local nlohmann_json from ${NLOHMANN_JSON_LOCAL}")
    set(nlohmann_json_SOURCE_DIR ${NLOHMANN_JSON_LOCAL})
    add_subdirectory(${NLOHMANN_JSON_LOCAL} ${CMAKE_BINARY_DIR}/nlohmann_json-build)
else()
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
        UPDATE_DISCONNECTED TRUE
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

# liblo for OSC support
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBLO REQUIRED liblo)

# PortAudio for audio I/O
pkg_check_modules(PORTAUDIO REQUIRED portaudio-2.0)

# FFTW3 for FFT processing
pkg_check_modules(FFTW REQUIRED fftw3f)

set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)
set(IMGUI_BACKENDS_DIR ${IMGUI_DIR}/backends)

add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_BACKENDS_DIR}/imgui_impl_sdl2.cpp
    ${IMGUI_BACKENDS_DIR}/imgui_impl_sdlrenderer2.cpp
)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_BACKENDS_DIR})
target_link_libraries(imgui PUBLIC SDL2::SDL2)

set(UBO_LAYOUT_HEADER ${CMAKE_CURRENT_SOURCE_DIR}/src/ubo_layout.h)
set(SHARED_UBO_FILE ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shared_ubo.glsl)
add_custom_command(
    OUTPUT ${SHARED_UBO_FILE}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_ubo_glsl.py
    DEPENDS ${UBO_LAYOUT_HEADER} ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_ubo_glsl.py
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Generating shared_ubo.glsl from src/ubo_layout.h"
)

add_custom_target(gen_ubo DEPENDS ${SHARED_UBO_FILE})

add_executable(app
    src/main.cpp
    src/app/Application.cpp
    src/app/UISystem.cpp
    src/app/ControlState.cpp
    src/app/MidiSystem.cpp
    src/app/OscSystem.cpp
    src/app/AudioSystem.cpp
    src/app/parameters/ParameterRegistry.cpp
    src/app/parameters/VisualControlsRegistry.cpp
    src/app/parameters/ParameterBindingRegistry.cpp
    src/app/parameters/JsonSerializer.cpp
    src/app/parameters/MidiBindingSystem.cpp
    src/app/parameters/PresetMorpher.cpp
    src/core/Window.cpp
    src/core/VulkanContext.cpp
    src/core/VulkanPresenter.cpp
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
)

add_dependencies(app gen_ubo)

# --- Automatic compute shader generation and compilation ---
find_program(GLSLC glslc REQUIRED)

set(PASS_FRAGS
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/pass_a_base.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/pass_b_spatial.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/pass_c_detail.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/pass_d_temporal.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/pass_e_degradation.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/pass_f_color.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/pass_g_output.frag
)

set(PASS_COMP_FILES)
set(PASS_COMP_SPV)
foreach(frag ${PASS_FRAGS})
    get_filename_component(name ${frag} NAME_WE)
    set(comp ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${name}.comp)
    set(spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${name}.comp.spv)
    list(APPEND PASS_COMP_FILES ${comp})
    list(APPEND PASS_COMP_SPV ${spv})
endforeach()

add_custom_command(
    OUTPUT ${PASS_COMP_FILES}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/frag_to_compute.py
    DEPENDS ${PASS_FRAGS} ${CMAKE_CURRENT_SOURCE_DIR}/tools/frag_to_compute.py
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Generating compute shaders from fragment shaders"
)

foreach(frag ${PASS_FRAGS})
    get_filename_component(name ${frag} NAME_WE)
    set(comp ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${name}.comp)
    set(spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${name}.comp.spv)
    add_custom_command(
        OUTPUT ${spv}
        COMMAND ${GLSLC} -fshader-stage=compute ${comp} -o ${spv}
        DEPENDS ${comp} ${PASS_FRAGS}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Compiling compute shader ${name}.comp"
    )
endforeach()

add_custom_target(compile_compute_shaders DEPENDS ${PASS_COMP_SPV})
add_dependencies(app compile_compute_shaders)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${LIBLO_INCLUDE_DIRS} ${PORTAUDIO_INCLUDE_DIRS} ${FFTW_INCLUDE_DIRS} ${kfr_SOURCE_DIR}/include ${nlohmann_json_SOURCE_DIR}/include)

target_link_libraries(app
    Vulkan::Vulkan
    SDL2::SDL2
    glm::glm
    PkgConfig::FFMPEG
    rtmidi
    kfr
    imgui
    ${LIBLO_LIBRARIES}
    ${PORTAUDIO_LIBRARIES}
    ${FFTW_LIBRARIES}
)
