cmake_minimum_required(VERSION 3.16)
project(sdlrenderer)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIGURATION>)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIGURATION>)

# prevent installing to system directories. 
set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}" CACHE INTERNAL "")

add_executable(
    sdlrenderer
    source/main.cpp
    source/renderer.cpp
)

target_compile_features(sdlrenderer PUBLIC cxx_std_20)

set(VULKAN_PATH "C:/VulkanSDK/1.3.280.0")
include_directories(${VULKAN_PATH}/Include)
link_directories(${VULKAN_PATH}/Lib)
target_link_libraries(sdlrenderer ${VULKAN_PATH}/Lib/vulkan-1.lib)

set(SDL_HIDAPI OFF)
set(SDL_HIDAPI_JOYSTICK OFF)
set(SDL_HIDAPI_LIBUSB OFF)
set(SDL_HIDAPI_LIBUSB_SHARED OFF)
add_subdirectory(SDL EXCLUDE_FROM_ALL)
target_link_libraries(sdlrenderer SDL3::SDL3)
