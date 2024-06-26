#include "d3dx12/d3dx12.h"
#include "math.h"
#include "renderer.h"
#include <D3Dcompiler.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <dxgidebug.h>
#include <windows.h>

static const UINT FrameCount = 2;

struct Vertex {
  float3 position;
  float4 color;
};

// Pipeline objects.
CD3DX12_VIEWPORT m_viewport;
CD3DX12_RECT m_scissorRect;
IDXGISwapChain3* m_swapChain;
ID3D12Device* m_device;
ID3D12Resource* m_renderTargets[FrameCount];
ID3D12CommandAllocator* m_commandAllocator;
ID3D12CommandQueue* m_commandQueue;
ID3D12RootSignature* m_rootSignature;
ID3D12DescriptorHeap* m_rtvHeap;
ID3D12PipelineState* m_pipelineState;
ID3D12GraphicsCommandList* m_commandList;
UINT m_rtvDescriptorSize;

// App resources.
ID3D12Resource* m_vertexBuffer;
D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;

// Synchronization objects.
UINT m_frameIndex;
HANDLE m_fenceEvent;
ID3D12Fence* m_fence;
UINT64 m_fenceValue;

void WaitForPreviousFrame();
void GetHardwareAdapter(IDXGIFactory1* pFactory, IDXGIAdapter1** ppAdapter);
void PopulateCommandList();

SDL_WindowFlags Renderer::GetRequiredWindowFlags() { return 0; }

void EnableDebugLayer(UINT* dxgiFactoryFlags) {
  // Enable the debug layer (requires the Graphics Tools "optional feature").
  // NOTE: Enabling the debug layer after device creation will invalidate the active device.
  ID3D12Debug* debugController;
  if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
    debugController->EnableDebugLayer();
    *dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
  }
}

void CreateDescriptorHeaps() {
  D3D12_DESCRIPTOR_HEAP_DESC renderTargetViewHeapDesc = {
      .NumDescriptors = FrameCount,
      .Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
      .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
  };
  m_device->CreateDescriptorHeap(&renderTargetViewHeapDesc, IID_PPV_ARGS(&m_rtvHeap));

  m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
}

void CreateSwapChain(HWND hwnd, int width, int height, UINT* dxgiFactoryFlags) {
  IDXGIFactory4* factory = NULL;
  CreateDXGIFactory2(*dxgiFactoryFlags, IID_PPV_ARGS(&factory));

  IDXGIAdapter1* hardwareAdapter = NULL;
  GetHardwareAdapter(factory, &hardwareAdapter);

  D3D12CreateDevice(hardwareAdapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_device));

  // Describe and create the command queue.
  D3D12_COMMAND_QUEUE_DESC queueDesc = {
      .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
      .Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
  };
  m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue));

  // Describe and create the swap chain.
  DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
  swapChainDesc.BufferCount = FrameCount;
  swapChainDesc.Width = width;
  swapChainDesc.Height = height;
  swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  swapChainDesc.SampleDesc.Count = 1;

  IDXGISwapChain1* swapChain;
  factory->CreateSwapChainForHwnd(
      m_commandQueue, // Swap chain needs the queue so that it can force a flush on it.
      hwnd, &swapChainDesc, NULL, NULL, &swapChain);

  // This sample does not support fullscreen transitions.
  factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

  m_swapChain = (IDXGISwapChain3*)swapChain;
  m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}

void CreateFrameResources() {
  CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

  for (UINT n = 0; n < FrameCount; n++) {
    m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n]));
    m_device->CreateRenderTargetView(m_renderTargets[n], NULL, rtvHandle);
    rtvHandle.Offset(1, m_rtvDescriptorSize);
  }
}

void CreateRootSignature() {
  CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
  rootSignatureDesc.Init(0, NULL, 0, NULL, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

  ID3DBlob* sign;
  ID3DBlob* error;
  D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sign, &error);
  m_device->CreateRootSignature(0, sign->GetBufferPointer(), sign->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature));
}

void CreatePipelineState() {
  ID3DBlob* vertexShader = NULL;
  ID3DBlob* pixelShader = NULL;

  // Enable better shader debugging with the graphics debugging tools.
  UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;

  auto shaderPath = L"D:\\Works\\SDLs\\sdlrenderer\\resources\\shaders.hlsl";
  D3DCompileFromFile(shaderPath, NULL, NULL, "VSMain", "vs_5_0", compileFlags, 0, &vertexShader, NULL);
  D3DCompileFromFile(shaderPath, NULL, NULL, "PSMain", "ps_5_0", compileFlags, 0, &pixelShader, NULL);

  // Define the vertex input layout.
  D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};

  // Describe and create the graphics pipeline state object (PSO).
  D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
  psoDesc.InputLayout = {inputElementDescs, _countof(inputElementDescs)};
  psoDesc.pRootSignature = m_rootSignature;
  psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader);
  psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader);
  psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
  psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
  psoDesc.DepthStencilState.DepthEnable = FALSE;
  psoDesc.DepthStencilState.StencilEnable = FALSE;
  psoDesc.SampleMask = UINT_MAX;
  psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  psoDesc.NumRenderTargets = 1;
  psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  psoDesc.SampleDesc.Count = 1;
  m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState));
}

void CreateVertexBuffer() {
  Vertex triangleVertices[] = {
      {{0.0f, 0.25f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
      {{0.25f, -0.25f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
      {{-0.25f, -0.25f, 0.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},
  };

  const UINT vertexBufferSize = sizeof(triangleVertices);

  // Note: using upload heaps to transfer static data like vert buffers is not
  // recommended. Every time the GPU needs it, the upload heap will be marshalled
  // over. Please read up on Default Heap usage. An upload heap is used here for
  // code simplicity and because there are very few verts to actually transfer.
  const auto heapProp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
  const auto vertBuff = CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize);
  m_device->CreateCommittedResource(
      &heapProp, D3D12_HEAP_FLAG_NONE, &vertBuff, D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
      IID_PPV_ARGS(&m_vertexBuffer));

  // Copy the triangle data to the vertex buffer.
  UINT8* pVertexDataBegin;
  CD3DX12_RANGE readRange(0, 0); // We do not intend to read from this resource on the CPU.
  m_vertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin));
  memcpy(pVertexDataBegin, triangleVertices, sizeof(triangleVertices));
  m_vertexBuffer->Unmap(0, NULL);

  // Initialize the vertex buffer view.
  m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
  m_vertexBufferView.StrideInBytes = sizeof(Vertex);
  m_vertexBufferView.SizeInBytes = vertexBufferSize;
}

void CreateSyncObjects() {
  m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
  m_fenceValue = 1;

  m_fenceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
  if (m_fenceEvent == NULL) {
    HRESULT_FROM_WIN32(GetLastError());
  }

  // Wait for the command list to execute; we are reusing the same command
  // list in our main loop but for now, we just want to wait for setup to
  // complete before continuing.
  WaitForPreviousFrame();
}

Renderer::Renderer(SDL_Window* window) {
  int width;
  int height;
  SDL_GetWindowSize(window, &width, &height);

  SDL_PropertiesID windowProperties = SDL_GetWindowProperties(window);
  HWND hwnd = (HWND)SDL_GetProperty(windowProperties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);

  UINT dxgiFactoryFlags = 0;
  EnableDebugLayer(&dxgiFactoryFlags);
  CreateSwapChain(hwnd, width, height, &dxgiFactoryFlags);

  CreateDescriptorHeaps();

  CreateFrameResources();

  m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator));

  CreateRootSignature();

  CreatePipelineState();

  m_device->CreateCommandList(
      0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator, m_pipelineState, IID_PPV_ARGS(&m_commandList));
  m_commandList->Close();

  CreateVertexBuffer();

  CreateSyncObjects();

  m_viewport.TopLeftX = 0;
  m_viewport.TopLeftY = 0;
  m_viewport.Width = width;
  m_viewport.Height = height;

  m_scissorRect.left = 0;
  m_scissorRect.top = 0;
  m_scissorRect.right = width;
  m_scissorRect.bottom = height;
}

int Renderer::Present() {
  // Record all the commands we need to render the scene into the command list.
  PopulateCommandList();

  // Execute the command list.
  ID3D12CommandList* ppCommandLists[] = {m_commandList};
  m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

  // Present the frame.
  m_swapChain->Present(1, 0);

  WaitForPreviousFrame();

  return 0;
}

void WaitForPreviousFrame() {
  // WAITING FOR THE FRAME TO COMPLETE BEFORE CONTINUING IS NOT BEST PRACTICE.
  // This is code implemented as such for simplicity. The D3D12HelloFrameBuffering
  // sample illustrates how to use fences for efficient resource usage and to
  // maximize GPU utilization.

  // Signal and increment the fence value.
  const UINT64 fence = m_fenceValue;
  m_commandQueue->Signal(m_fence, fence);
  m_fenceValue++;

  // Wait until the previous frame is finished.
  if (m_fence->GetCompletedValue() < fence) {
    m_fence->SetEventOnCompletion(fence, m_fenceEvent);
    WaitForSingleObject(m_fenceEvent, INFINITE);
  }

  m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}

void GetHardwareAdapter(IDXGIFactory1* pFactory, IDXGIAdapter1** ppAdapter) {
  *ppAdapter = NULL;

  IDXGIAdapter1* adapter = NULL;
  IDXGIFactory6* factory6 = NULL;
  if (SUCCEEDED(pFactory->QueryInterface(IID_PPV_ARGS(&factory6)))) {
    for (UINT adapterIndex = 0; SUCCEEDED(factory6->EnumAdapterByGpuPreference(
             adapterIndex, DXGI_GPU_PREFERENCE_UNSPECIFIED, IID_PPV_ARGS(&adapter)));
         ++adapterIndex) {
      DXGI_ADAPTER_DESC1 desc;
      adapter->GetDesc1(&desc);

      if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
        continue;
      }

      if (SUCCEEDED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_0, _uuidof(ID3D12Device), NULL))) {
        break;
      }
    }
  }

  if (adapter == NULL) {
    for (UINT adapterIndex = 0; SUCCEEDED(pFactory->EnumAdapters1(adapterIndex, &adapter)); ++adapterIndex) {
      DXGI_ADAPTER_DESC1 desc;
      adapter->GetDesc1(&desc);

      if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
        continue;
      }

      if (SUCCEEDED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_0, _uuidof(ID3D12Device), NULL))) {
        break;
      }
    }
  }

  *ppAdapter = adapter;
}

void PopulateCommandList() {
  // Command list allocators can only be reset when the associated
  // command lists have finished execution on the GPU; apps should use
  // fences to determine GPU execution progress.
  m_commandAllocator->Reset();

  // However, when ExecuteCommandList() is called on a particular command
  // list, that command list can then be reset at any time and must be before
  // re-recording.
  m_commandList->Reset(m_commandAllocator, m_pipelineState);

  // Set necessary state.
  m_commandList->SetGraphicsRootSignature(m_rootSignature);
  m_commandList->RSSetViewports(1, &m_viewport);
  m_commandList->RSSetScissorRects(1, &m_scissorRect);

  // Indicate that the back buffer will be used as a render target.
  const auto resBar = CD3DX12_RESOURCE_BARRIER::Transition(
      m_renderTargets[m_frameIndex], D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
  m_commandList->ResourceBarrier(1, &resBar);

  CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(
      m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize);
  m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, NULL);

  // Record commands.
  const float clearColor[] = {0.0f, 0.0f, 0.0f, 1.0f};
  m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, NULL);
  m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
  m_commandList->DrawInstanced(3, 1, 0, 0);

  // Indicate that the back buffer will now be used to present.
  const auto resBar1 = CD3DX12_RESOURCE_BARRIER::Transition(
      m_renderTargets[m_frameIndex], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
  m_commandList->ResourceBarrier(1, &resBar1);

  m_commandList->Close();
}

Renderer::~Renderer() {
  // Ensure that the GPU is no longer referencing resources that are about to be
  // cleaned up by the destructor.
  WaitForPreviousFrame();

  CloseHandle(m_fenceEvent);
}
