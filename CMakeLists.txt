cmake_minimum_required(VERSION 3.10)
project(VulkanApp)

set(CMAKE_CXX_STANDARD 17)

find_package(Vulkan REQUIRED)
find_package(SDL2 REQUIRED)

add_executable(app main.cpp)

target_link_libraries(app Vulkan::Vulkan SDL2::SDL2)
