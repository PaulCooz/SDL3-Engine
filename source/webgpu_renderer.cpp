#include "renderer.h"
#include <webgpu.h>

WGPUInstance instance;
WGPUDevice device;
WGPUSwapChain swapChain;
WGPURenderPipeline pipeline;
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

void GetDevice(void (*callback)(WGPUDevice)) {
  wgpuInstanceRequestAdapter(
      instance, nullptr,
      [](WGPURequestAdapterStatus status, WGPUAdapter adapter, const char* message, void* userdata) {
        if (status != WGPURequestAdapterStatus_Success) {
          return;
        }
        wgpuAdapterRequestDevice(
            adapter, nullptr,
            [](WGPURequestDeviceStatus status, WGPUDevice device, const char* message, void* userdata) {
              wgpuDeviceSetUncapturedErrorCallback(
                  device,
                  [](WGPUErrorType type, const char* message, void* userdata) {
                    SDL_LogError(SDL_LOG_CATEGORY_VIDEO, "Error: %u - message: %s", type, message);
                  },
                  nullptr);
              reinterpret_cast<void (*)(WGPUDevice)>(userdata)(device);
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

  instance = wgpuCreateInstance(nullptr);
  GetDevice([](WGPUDevice dev) {
    device = dev;

    WGPUSurfaceDescriptorFromCanvasHTMLSelector canvasDesc{
        .chain = {.next = nullptr, .sType = WGPUSType_SurfaceDescriptorFromCanvasHTMLSelector},
        .selector = "#canvas",
    };
    WGPUSurfaceDescriptor surfaceDesc{.nextInChain = &canvasDesc.chain};
    WGPUSurface surface = wgpuInstanceCreateSurface(instance, &surfaceDesc);

    WGPUSwapChainDescriptor scDesc{
        .nextInChain = nullptr,
        .usage = WGPUTextureUsage_RenderAttachment,
        .format = WGPUTextureFormat_BGRA8Unorm,
        .width = kWidth,
        .height = kHeight,
        .presentMode = WGPUPresentMode_Fifo};
    swapChain = wgpuDeviceCreateSwapChain(device, surface, &scDesc);
    WGPUShaderModuleWGSLDescriptor wgslDesc{
        .chain = {.next = nullptr, .sType = WGPUSType_ShaderModuleWGSLDescriptor},
        .code = shaderCode,
    };
    WGPUShaderModuleDescriptor shaderModuleDescriptor{.nextInChain = &wgslDesc.chain};
    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(device, &shaderModuleDescriptor);

    WGPUPipelineLayoutDescriptor plDescr{.bindGroupLayoutCount = 0, .bindGroupLayouts = nullptr};
    WGPUColorTargetState colorTargetState{.format = WGPUTextureFormat_BGRA8Unorm, .writeMask = WGPUColorWriteMask_All};
    WGPUFragmentState fragmentState{.module = shaderModule, .targetCount = 1, .targets = &colorTargetState};
    WGPURenderPipelineDescriptor descriptor{
        .layout = wgpuDeviceCreatePipelineLayout(device, &plDescr),
        .vertex = {.module = shaderModule},
        .primitive =
            {
                .topology = WGPUPrimitiveTopology_TriangleList,
                .stripIndexFormat = WGPUIndexFormat_Undefined,
                .frontFace = WGPUFrontFace_CCW,
                .cullMode = WGPUCullMode_None,
            },
        .multisample = {.nextInChain = nullptr, .count = 1, .mask = UINT32_MAX, .alphaToCoverageEnabled = false},
        .fragment = &fragmentState,
    };
    pipeline = wgpuDeviceCreateRenderPipeline(device, &descriptor);

    inited = true;
  });
}

int Renderer::Present() {
  if (!inited)
    return 0;

  WGPURenderPassColorAttachment attachment{
      .view = wgpuSwapChainGetCurrentTextureView(swapChain),
      .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
      .loadOp = WGPULoadOp_Clear,
      .storeOp = WGPUStoreOp_Store,
  };
  WGPURenderPassDescriptor renderpass{.colorAttachmentCount = 1, .colorAttachments = &attachment};

  WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, nullptr);
  WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &renderpass);
  wgpuRenderPassEncoderSetPipeline(pass, pipeline);
  wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
  wgpuRenderPassEncoderEnd(pass);
  WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, nullptr);
  WGPUQueue queue = wgpuDeviceGetQueue(device);
  wgpuQueueSubmit(queue, 1, &commands);

  return 0;
}

Renderer::~Renderer() {}
