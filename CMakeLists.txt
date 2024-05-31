cmake_minimum_required(VERSION 3.16)
project(sdlrenderer)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIGURATION>)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIGURATION>)

# prevent installing to system directories. 
set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}" CACHE INTERNAL "")

add_executable(
    sdlrenderer
    source/main.cpp
    # source/vulkan_renderer.cpp
    # source/direct12_renderer.cpp
    source/webgpu_renderer.cpp
)

target_compile_features(sdlrenderer PUBLIC cxx_std_20)

# set(VULKAN_PATH "C:/VulkanSDK/1.3.280.0")
# include_directories(${VULKAN_PATH}/Include)
# link_directories(${VULKAN_PATH}/Lib)
# target_link_libraries(sdlrenderer ${VULKAN_PATH}/Lib/vulkan-1.lib)

target_compile_definitions(sdlrenderer PRIVATE "UNICODE" "_UNICODE")

# set(D3DX12_PATH "D:/Windows Kits/10")
# include_directories("${D3DX12_PATH}/Include/10.0.22000.0/um")
# include_directories("${D3DX12_PATH}/Include/10.0.22000.0/shared")
# link_directories("${D3DX12_PATH}/Lib/10.0.22000.0/um/x64")
# https://globalcdn.nuget.org/packages/microsoft.direct3d.d3d12.1.613.1.nupkg
# include_directories(sdlrenderer "third_party/D3D12/build/native/include")
# link_directories(sdlrenderer "third_party/D3D12/build/native/bin/x64")
# target_link_libraries(sdlrenderer "d3d12.lib" "dxgi.lib" "d3dcompiler.lib")

if(EMSCRIPTEN)
    include_directories(sdlrenderer "third_party/webgpu-headers")
    # Add Emscripten-specific link options
    target_link_options(sdlrenderer PRIVATE
        -sUSE_WEBGPU # Handle WebGPU symbols
        -sASYNCIFY # Required by WebGPU-C++
    )
    set_target_properties(sdlrenderer PROPERTIES SUFFIX ".html")
    # target_link_options(sdlrenderer PRIVATE
        # [...]
        # --preload-file "${CMAKE_CURRENT_SOURCE_DIR}/resources"
    # )
endif()

set(SDL_HIDAPI OFF)
set(SDL_HIDAPI_JOYSTICK OFF)
set(SDL_HIDAPI_LIBUSB OFF)
set(SDL_HIDAPI_LIBUSB_SHARED OFF)
add_subdirectory("third_party/SDL" EXCLUDE_FROM_ALL)
target_link_libraries(sdlrenderer SDL3::SDL3)
