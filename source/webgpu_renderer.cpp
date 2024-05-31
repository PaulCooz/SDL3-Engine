#include "renderer.h"
#include <cassert>
#include <iostream>
#include <vector>
#include <webgpu/webgpu_cpp.h>

wgpu::Instance instance;
wgpu::Device device;
wgpu::SwapChain swapChain;
wgpu::RenderPipeline pipeline;
bool inited = false;

const char shaderCode[] = R"(
    @vertex fn vertexMain(@builtin(vertex_index) i : u32) ->
      @builtin(position) vec4f {
        const pos = array(vec2f(0, 1), vec2f(-1, -1), vec2f(1, -1));
        return vec4f(pos[i], 0, 1);
    }
    @fragment fn fragmentMain() -> @location(0) vec4f {
        return vec4f(0.3, 0.4, 1, 1);
    }
)";

void GetDevice(void (*callback)(wgpu::Device)) {
  instance.RequestAdapter(
      nullptr,
      [](WGPURequestAdapterStatus status, WGPUAdapter cAdapter, const char* message, void* userdata) {
        if (status != WGPURequestAdapterStatus_Success) {
          exit(0);
        }
        wgpu::Adapter adapter = wgpu::Adapter::Acquire(cAdapter);
        adapter.RequestDevice(
            nullptr,
            [](WGPURequestDeviceStatus status, WGPUDevice cDevice, const char* message, void* userdata) {
              wgpu::Device device = wgpu::Device::Acquire(cDevice);
              device.SetUncapturedErrorCallback(
                  [](WGPUErrorType type, const char* message, void* userdata) {
                    std::cout << "Error: " << type << " - message: " << message;
                  },
                  nullptr);
              reinterpret_cast<void (*)(wgpu::Device)>(userdata)(device);
            },
            userdata);
      },
      reinterpret_cast<void*>(callback));
}

SDL_WindowFlags Renderer::GetRequiredWindowFlags() { return 0; }

Uint32 kWidth;
Uint32 kHeight;

Renderer::Renderer(SDL_Window* window) {
  int w, h;
  SDL_GetWindowSize(window, &w, &h);
  kWidth = (Uint32)w;
  kHeight = (Uint32)h;

  instance = wgpu::CreateInstance();
  GetDevice([](wgpu::Device dev) {
    device = dev;
    wgpu::SurfaceDescriptorFromCanvasHTMLSelector canvasDesc{};
    canvasDesc.selector = "#canvas";

    wgpu::SurfaceDescriptor surfaceDesc{.nextInChain = &canvasDesc};
    wgpu::Surface surface = instance.CreateSurface(&surfaceDesc);

    wgpu::SwapChainDescriptor scDesc{
        .usage = wgpu::TextureUsage::RenderAttachment,
        .format = wgpu::TextureFormat::BGRA8Unorm,
        .width = kWidth,
        .height = kHeight,
        .presentMode = wgpu::PresentMode::Fifo};
    swapChain = device.CreateSwapChain(surface, &scDesc);
    wgpu::ShaderModuleWGSLDescriptor wgslDesc{};
    wgslDesc.code = shaderCode;

    wgpu::ShaderModuleDescriptor shaderModuleDescriptor{.nextInChain = &wgslDesc};
    wgpu::ShaderModule shaderModule = device.CreateShaderModule(&shaderModuleDescriptor);

    wgpu::ColorTargetState colorTargetState{.format = wgpu::TextureFormat::BGRA8Unorm};

    wgpu::FragmentState fragmentState{.module = shaderModule, .targetCount = 1, .targets = &colorTargetState};

    wgpu::RenderPipelineDescriptor descriptor{.vertex = {.module = shaderModule}, .fragment = &fragmentState};
    pipeline = device.CreateRenderPipeline(&descriptor);

    inited = true;
  });
}

int Renderer::Present() {
  if (!inited)
    return 0;

  wgpu::RenderPassColorAttachment attachment{
      .view = swapChain.GetCurrentTextureView(), .loadOp = wgpu::LoadOp::Clear, .storeOp = wgpu::StoreOp::Store};

  wgpu::RenderPassDescriptor renderpass{.colorAttachmentCount = 1, .colorAttachments = &attachment};

  wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
  wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&renderpass);
  pass.SetPipeline(pipeline);
  pass.Draw(3);
  pass.End();
  wgpu::CommandBuffer commands = encoder.Finish();
  device.GetQueue().Submit(1, &commands);
  return 0;
}

Renderer::~Renderer() {}
