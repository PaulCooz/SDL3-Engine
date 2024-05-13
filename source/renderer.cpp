#include "renderer.h"

#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>

#include <fstream>
#include <string>
#include <vector>

#define VULKAN_VALIDATION_LAYER_NAME "VK_LAYER_KHRONOS_validation"
#define VK_INST_FUNC(inst, name) (PFN_##name) vkGetInstanceProcAddr(inst, #name)

typedef struct {
  VkInstance instance;
  VkDebugUtilsMessengerEXT debugMessenger;

  VkPhysicalDevice physicalDevice;
  VkDevice device;

  Uint32 deviceGraphicsQueueIndex;
  Uint32 devicePresentQueueIndex;
  VkQueue graphicsQueue;
  VkQueue presentQueue;

  VkSurfaceKHR surface;
} RenderData;

static RenderData* renderData;

const Uint32 countInstLayers = 1;
const char* const instLayers[] = {VULKAN_VALIDATION_LAYER_NAME};
const Uint32 countDeviceLayers = 1;
const char* const deviceLayers[] = {VULKAN_VALIDATION_LAYER_NAME};
const Uint32 deviceExtCount = 1;
const char* const deviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

VkSwapchainKHR swapChain;
VkFormat swapChainImageFormat;
VkExtent2D swapChainExtent;
std::vector<VkImage> swapChainImages;
std::vector<VkImageView> swapChainImageViews;
VkPipelineLayout pipelineLayout;
VkPipeline pipeline;
VkRenderPass renderPass;
std::vector<VkFramebuffer> swapChainFramebuffers;
VkCommandPool commandPool;
std::vector<VkCommandBuffer> commandBuffers;
std::vector<VkSemaphore> imageAvailableSemaphores;
std::vector<VkSemaphore> renderFinishedSemaphores;
std::vector<VkFence> inFlightFences;

uint32_t currentFrame = 0;
const static int MAX_FRAMES_IN_FLIGHT = 2;

void createSwapChain();

static VKAPI_ATTR VkBool32 VKAPI_CALL DebugMessenger(
    VkDebugUtilsMessageSeverityFlagBitsEXT severityBits, VkDebugUtilsMessageTypeFlagsEXT typeFlags,
    const VkDebugUtilsMessengerCallbackDataEXT* data, void* userData) {
  if ((severityBits & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0) {
    SDL_LogError(SDL_LOG_CATEGORY_RENDER, data->pMessage);
  } else if ((severityBits & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0) {
    SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, data->pMessage);
  }
  return VK_FALSE;
}

static std::vector<char> readFile(const std::string& filename) {
  std::ifstream file(filename, std::ios::ate | std::ios::binary);

  size_t fileSize = (size_t)file.tellg();
  std::vector<char> buffer(fileSize);
  file.seekg(0);
  file.read(buffer.data(), fileSize);
  file.close();

  return buffer;
}

void cleanupSwapChain() {
  for (auto framebuffer : swapChainFramebuffers) {
    vkDestroyFramebuffer(renderData->device, framebuffer, NULL);
  }
  for (auto imageView : swapChainImageViews) {
    vkDestroyImageView(renderData->device, imageView, NULL);
  }
  vkDestroySwapchainKHR(renderData->device, swapChain, NULL);
}

void createSwapChain() {
  VkSurfaceCapabilitiesKHR capabilities;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(renderData->physicalDevice, renderData->surface, &capabilities);

  Uint32 formatCount;
  VkSurfaceFormatKHR* formats;
  vkGetPhysicalDeviceSurfaceFormatsKHR(renderData->physicalDevice, renderData->surface, &formatCount, NULL);
  formats = (VkSurfaceFormatKHR*)SDL_malloc(formatCount * sizeof(VkSurfaceFormatKHR));
  vkGetPhysicalDeviceSurfaceFormatsKHR(renderData->physicalDevice, renderData->surface, &formatCount, formats);

  Uint32 presentModeCount;
  VkPresentModeKHR* presentModes;
  vkGetPhysicalDeviceSurfacePresentModesKHR(renderData->physicalDevice, renderData->surface, &presentModeCount, NULL);
  presentModes = (VkPresentModeKHR*)SDL_malloc(presentModeCount * sizeof(VkPresentModeKHR));
  vkGetPhysicalDeviceSurfacePresentModesKHR(
      renderData->physicalDevice, renderData->surface, &presentModeCount, presentModes);

  VkSwapchainCreateInfoKHR createInfo = {
      .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
      .surface = renderData->surface,
      .minImageCount = capabilities.minImageCount,
      .imageFormat = formats[0].format,
      .imageColorSpace = formats[0].colorSpace,
      .imageExtent = capabilities.currentExtent,
      .imageArrayLayers = 1,
      .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
      .preTransform = capabilities.currentTransform,
      .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
      .presentMode = VK_PRESENT_MODE_FIFO_KHR,
      .clipped = VK_TRUE,
      .oldSwapchain = VK_NULL_HANDLE,
  };
  if (renderData->deviceGraphicsQueueIndex != renderData->devicePresentQueueIndex) {
    Uint32 queueFamilyIndices[] = {renderData->deviceGraphicsQueueIndex, renderData->devicePresentQueueIndex};
    createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
    createInfo.queueFamilyIndexCount = 2;
    createInfo.pQueueFamilyIndices = queueFamilyIndices;
  } else {
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.queueFamilyIndexCount = 0;
    createInfo.pQueueFamilyIndices = NULL;
  }
  vkCreateSwapchainKHR(renderData->device, &createInfo, NULL, &swapChain);

  Uint32 imageCount = 0;
  vkGetSwapchainImagesKHR(renderData->device, swapChain, &imageCount, NULL);
  swapChainImages.resize(imageCount);
  vkGetSwapchainImagesKHR(renderData->device, swapChain, &imageCount, swapChainImages.data());

  swapChainImageFormat = formats[0].format;
  swapChainExtent = capabilities.currentExtent;

  swapChainImageViews.resize(swapChainImages.size());
  for (size_t i = 0; i < swapChainImages.size(); i++) {
    VkImageViewCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    createInfo.image = swapChainImages[i];
    createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    createInfo.format = swapChainImageFormat;
    createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    createInfo.subresourceRange.baseMipLevel = 0;
    createInfo.subresourceRange.levelCount = 1;
    createInfo.subresourceRange.baseArrayLayer = 0;
    createInfo.subresourceRange.layerCount = 1;

    vkCreateImageView(renderData->device, &createInfo, NULL, &swapChainImageViews[i]);
  }

  swapChainFramebuffers.resize(swapChainImageViews.size());
  for (size_t i = 0; i < swapChainImageViews.size(); i++) {
    VkImageView attachments[] = {swapChainImageViews[i]};

    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = renderPass;
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments = attachments;
    framebufferInfo.width = swapChainExtent.width;
    framebufferInfo.height = swapChainExtent.height;
    framebufferInfo.layers = 1;

    vkCreateFramebuffer(renderData->device, &framebufferInfo, NULL, &swapChainFramebuffers[i]);
  }

  SDL_free(formats);
  SDL_free(presentModes);
}

void recreateSwapChain() {
  vkDeviceWaitIdle(renderData->device);

  cleanupSwapChain();

  createSwapChain();
}

void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

  vkBeginCommandBuffer(commandBuffer, &beginInfo);

  VkRenderPassBeginInfo renderPassInfo{};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  renderPassInfo.renderPass = renderPass;
  renderPassInfo.framebuffer = swapChainFramebuffers[imageIndex];
  renderPassInfo.renderArea.offset = {0, 0};
  renderPassInfo.renderArea.extent = swapChainExtent;

  VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
  renderPassInfo.clearValueCount = 1;
  renderPassInfo.pClearValues = &clearColor;

  vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

  VkViewport viewport{};
  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.width = (float)swapChainExtent.width;
  viewport.height = (float)swapChainExtent.height;
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

  VkRect2D scissor{};
  scissor.offset = {0, 0};
  scissor.extent = swapChainExtent;
  vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

  vkCmdDraw(commandBuffer, 3, 1, 0, 0);

  vkCmdEndRenderPass(commandBuffer);
  vkEndCommandBuffer(commandBuffer);
}

bool CheckRequiredInstLayers(const char* const* requiredLayers, Uint32 layersCount) {
  Uint32 propertyCount;
  vkEnumerateInstanceLayerProperties(&propertyCount, NULL);
  VkLayerProperties* properties = (VkLayerProperties*)SDL_malloc(propertyCount * sizeof(VkLayerProperties));
  vkEnumerateInstanceLayerProperties(&propertyCount, properties);

  bool hasAll = true;
  for (int i = 0; i < layersCount; i++) {
    bool missLayer = true;
    for (int j = 0; j < propertyCount; j++) {
      if (SDL_strcmp(requiredLayers[i], properties[j].layerName) == 0) {
        missLayer = false;
        break;
      }
    }
    if (missLayer) {
      SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Missing \"%s\" instance layer", requiredLayers[i]);
      hasAll = false;
    }
  }
  SDL_free(properties);
  return hasAll;
}

void CreateInstance() {
  Uint32 countSdlInstExt;
  const char* const* sdlInstExt = SDL_Vulkan_GetInstanceExtensions(&countSdlInstExt);
  Uint32 countInstExt = countSdlInstExt + 1;
  const char** instExt = (const char**)SDL_malloc(countInstExt * sizeof(const char*));
  SDL_memcpy((void*)instExt, sdlInstExt, countSdlInstExt * sizeof(const char*));
  instExt[countInstExt - 1] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;

  CheckRequiredInstLayers(instLayers, countInstLayers);

  VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = {
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
      .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
      .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_DEVICE_ADDRESS_BINDING_BIT_EXT,
      .pfnUserCallback = DebugMessenger,
  };

  VkInstanceCreateInfo instCreateInfo = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pNext = &debugCreateInfo,
      .pApplicationInfo = NULL,
      .enabledLayerCount = countInstLayers,
      .ppEnabledLayerNames = instLayers,
      .enabledExtensionCount = countInstExt,
      .ppEnabledExtensionNames = instExt,
  };

  vkCreateInstance(&instCreateInfo, NULL, &(renderData->instance));

  SDL_free(instExt);

  PFN_vkCreateDebugUtilsMessengerEXT createFunc = VK_INST_FUNC(renderData->instance, vkCreateDebugUtilsMessengerEXT);
  if (createFunc != NULL) {
    createFunc(renderData->instance, &debugCreateInfo, NULL, &(renderData->debugMessenger));
  } else {
    renderData->debugMessenger = NULL;
  }
}

bool HasRequiredDeviceLayers(VkPhysicalDevice physicalDevice, const char* const* requiredLayers, Uint32 layersCount) {
  Uint32 propertyCount;
  vkEnumerateDeviceLayerProperties(physicalDevice, &propertyCount, NULL);
  VkLayerProperties* properties = (VkLayerProperties*)SDL_malloc(propertyCount * sizeof(VkLayerProperties));
  vkEnumerateDeviceLayerProperties(physicalDevice, &propertyCount, properties);

  bool hasAll = true;
  for (int i = 0; i < layersCount; i++) {
    bool missLayer = true;
    for (int j = 0; j < propertyCount; j++) {
      if (SDL_strcmp(requiredLayers[i], properties[j].layerName) == 0) {
        missLayer = false;
        break;
      }
    }
    if (missLayer) {
      hasAll = false;
      break;
    }
  }
  SDL_free(properties);

  return hasAll;
}

void GetQueueFamilies(VkPhysicalDevice physicalDevice, Uint32* outGraphicsQueueI, Uint32* outPresentQueueI) {
  Uint32 queuePropCount;
  vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queuePropCount, NULL);
  VkQueueFamilyProperties* queueProps =
      (VkQueueFamilyProperties*)SDL_malloc(queuePropCount * sizeof(VkQueueFamilyProperties));
  vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queuePropCount, queueProps);

  for (int i = 0; i < queuePropCount; i++) {
    if ((queueProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
      *outGraphicsQueueI = i;
    }

    VkBool32 presentSupport = false;
    vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, renderData->surface, &presentSupport);
    if (presentSupport == VK_TRUE) {
      *outPresentQueueI = i;
    }
  }
  SDL_free(queueProps);
}

void PickPhysicalDeviceAndQueues() {
  Uint32 physicalDeviceCount;
  vkEnumeratePhysicalDevices(renderData->instance, &physicalDeviceCount, NULL);
  VkPhysicalDevice* physicalDevices = (VkPhysicalDevice*)SDL_malloc(sizeof(VkPhysicalDevice));
  vkEnumeratePhysicalDevices(renderData->instance, &physicalDeviceCount, physicalDevices);

  renderData->physicalDevice = VK_NULL_HANDLE;
  VkPhysicalDeviceProperties prevProps;
  for (int i = 0; i < physicalDeviceCount; i++) {
    VkPhysicalDevice deviceI = physicalDevices[i];
    VkPhysicalDeviceProperties currProps;
    VkPhysicalDeviceFeatures features;
    vkGetPhysicalDeviceProperties(deviceI, &currProps);
    vkGetPhysicalDeviceFeatures(deviceI, &features);

    Uint32 graphicsQueueI = UINT32_MAX;
    Uint32 presentQueueI = UINT32_MAX;
    GetQueueFamilies(deviceI, &graphicsQueueI, &presentQueueI);

    bool noCandidates = renderData->physicalDevice == VK_NULL_HANDLE;
    bool isSuitable = features.geometryShader && graphicsQueueI != UINT32_MAX && presentQueueI != UINT32_MAX &&
                      HasRequiredDeviceLayers(deviceI, deviceLayers, countDeviceLayers);
    bool betterType = noCandidates || (prevProps.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU &&
                                       currProps.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU);
    bool betterDim = noCandidates || (prevProps.limits.maxImageDimension2D < currProps.limits.maxImageDimension2D);
    if (isSuitable && (betterType || betterDim)) {
      renderData->physicalDevice = deviceI;
      renderData->deviceGraphicsQueueIndex = graphicsQueueI;
      renderData->devicePresentQueueIndex = presentQueueI;
      prevProps = currProps;
    }
  }

  SDL_free(physicalDevices);
}

void CreateDevices() {
  PickPhysicalDeviceAndQueues();

  Uint32 internalQueueCount = 1;
  float internalQueuePriorities[] = {1.0};
  VkDeviceQueueCreateInfo graphicsQueueInfo = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .pNext = NULL,
      .queueFamilyIndex = renderData->deviceGraphicsQueueIndex,
      .queueCount = internalQueueCount,
      .pQueuePriorities = internalQueuePriorities,
  };
  VkDeviceQueueCreateInfo presentQueueInfo = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .pNext = NULL,
      .queueFamilyIndex = renderData->devicePresentQueueIndex,
      .queueCount = internalQueueCount,
      .pQueuePriorities = internalQueuePriorities,
  };
  Uint32 queueInfoCount = 2;
  VkDeviceQueueCreateInfo queueInfos[] = {graphicsQueueInfo, presentQueueInfo};

  VkDeviceCreateInfo deviceInfo = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext = NULL,
      .queueCreateInfoCount = queueInfoCount,
      .pQueueCreateInfos = queueInfos,
      .enabledLayerCount = 0,
      .ppEnabledLayerNames = NULL,
      .enabledExtensionCount = deviceExtCount,
      .ppEnabledExtensionNames = deviceExtensions,
      .pEnabledFeatures = NULL,
  };

  vkCreateDevice(renderData->physicalDevice, &deviceInfo, NULL, &(renderData->device));

  vkGetDeviceQueue(renderData->device, renderData->deviceGraphicsQueueIndex, 0, &(renderData->graphicsQueue));
  vkGetDeviceQueue(renderData->device, renderData->devicePresentQueueIndex, 0, &(renderData->presentQueue));
}

Renderer::Renderer(SDL_Window* window) {
  renderData = (RenderData*)SDL_malloc(sizeof(RenderData));

  CreateInstance();
  SDL_Vulkan_CreateSurface(window, renderData->instance, NULL, &(renderData->surface));
  CreateDevices();

  // pipeline
  {
    Uint32 formatCount;
    VkSurfaceFormatKHR* formats;
    vkGetPhysicalDeviceSurfaceFormatsKHR(renderData->physicalDevice, renderData->surface, &formatCount, NULL);
    formats = (VkSurfaceFormatKHR*)SDL_malloc(formatCount * sizeof(VkSurfaceFormatKHR));
    vkGetPhysicalDeviceSurfaceFormatsKHR(renderData->physicalDevice, renderData->surface, &formatCount, formats);

    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = formats[0].format;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    vkCreateRenderPass(renderData->device, &renderPassInfo, NULL, &renderPass);

    SDL_free(formats);

    std::vector<char> vertShaderCode = readFile("vert.spv");
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = vertShaderCode.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(vertShaderCode.data());
    VkShaderModule vertShaderModule;
    vkCreateShaderModule(renderData->device, &createInfo, NULL, &vertShaderModule);

    std::vector<char> fragShaderCode = readFile("frag.spv");
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = fragShaderCode.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(fragShaderCode.data());
    VkShaderModule fragShaderModule;
    vkCreateShaderModule(renderData->device, &createInfo, NULL, &fragShaderModule);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 0;
    vertexInputInfo.vertexAttributeDescriptionCount = 0;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    colorBlending.blendConstants[0] = 0.0f;
    colorBlending.blendConstants[1] = 0.0f;
    colorBlending.blendConstants[2] = 0.0f;
    colorBlending.blendConstants[3] = 0.0f;

    std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 0;
    pipelineLayoutInfo.pushConstantRangeCount = 0;

    vkCreatePipelineLayout(renderData->device, &pipelineLayoutInfo, NULL, &pipelineLayout);

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    vkCreateGraphicsPipelines(renderData->device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &pipeline);

    vkDestroyShaderModule(renderData->device, fragShaderModule, NULL);
    vkDestroyShaderModule(renderData->device, vertShaderModule, NULL);
  }

  createSwapChain();

  // CommandPool
  {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = renderData->deviceGraphicsQueueIndex;
    vkCreateCommandPool(renderData->device, &poolInfo, NULL, &commandPool);
  }

  // Command Buffer
  {
    commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = (uint32_t)commandBuffers.size();

    vkAllocateCommandBuffers(renderData->device, &allocInfo, commandBuffers.data());
  }

  // Sync
  {
    imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
      vkCreateSemaphore(renderData->device, &semaphoreInfo, NULL, &imageAvailableSemaphores[i]);
      vkCreateSemaphore(renderData->device, &semaphoreInfo, NULL, &renderFinishedSemaphores[i]);
      vkCreateFence(renderData->device, &fenceInfo, NULL, &inFlightFences[i]);
    }
  }
}

int Renderer::Present() {
  vkWaitForFences(renderData->device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);

  uint32_t imageIndex;
  VkResult result = vkAcquireNextImageKHR(
      renderData->device, swapChain, UINT64_MAX, imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE, &imageIndex);

  if (result == VK_ERROR_OUT_OF_DATE_KHR) {
    recreateSwapChain();
    return 0;
  }

  vkResetFences(renderData->device, 1, &inFlightFences[currentFrame]);

  vkResetCommandBuffer(commandBuffers[currentFrame], 0);
  recordCommandBuffer(commandBuffers[currentFrame], imageIndex);

  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

  VkSemaphore waitSemaphores[] = {imageAvailableSemaphores[currentFrame]};
  VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
  submitInfo.waitSemaphoreCount = 1;
  submitInfo.pWaitSemaphores = waitSemaphores;
  submitInfo.pWaitDstStageMask = waitStages;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffers[currentFrame];

  VkSemaphore signalSemaphores[] = {renderFinishedSemaphores[currentFrame]};
  submitInfo.signalSemaphoreCount = 1;
  submitInfo.pSignalSemaphores = signalSemaphores;

  vkQueueSubmit(renderData->graphicsQueue, 1, &submitInfo, inFlightFences[currentFrame]);

  VkPresentInfoKHR presentInfo{};
  presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

  presentInfo.waitSemaphoreCount = 1;
  presentInfo.pWaitSemaphores = signalSemaphores;

  VkSwapchainKHR swapChains[] = {swapChain};
  presentInfo.swapchainCount = 1;
  presentInfo.pSwapchains = swapChains;
  presentInfo.pImageIndices = &imageIndex;
  presentInfo.pResults = NULL;

  result = vkQueuePresentKHR(renderData->presentQueue, &presentInfo);
  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
    recreateSwapChain();
  }
  currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
  return 0;
}

Renderer::~Renderer() {
  vkDeviceWaitIdle(renderData->device);

  cleanupSwapChain();
  for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    vkDestroySemaphore(renderData->device, renderFinishedSemaphores[i], nullptr);
    vkDestroySemaphore(renderData->device, imageAvailableSemaphores[i], nullptr);
    vkDestroyFence(renderData->device, inFlightFences[i], nullptr);
  }
  vkDestroyCommandPool(renderData->device, commandPool, NULL);

  vkDestroyPipeline(renderData->device, pipeline, NULL);
  vkDestroyPipelineLayout(renderData->device, pipelineLayout, NULL);
  vkDestroyRenderPass(renderData->device, renderPass, NULL);

  vkDestroySurfaceKHR(renderData->instance, renderData->surface, NULL);
  vkDestroyDevice(renderData->device, NULL);

  PFN_vkDestroyDebugUtilsMessengerEXT destroyFunc = VK_INST_FUNC(renderData->instance, vkDestroyDebugUtilsMessengerEXT);
  if (destroyFunc != NULL && renderData->debugMessenger != NULL) {
    destroyFunc(renderData->instance, renderData->debugMessenger, NULL);
  }

  vkDestroyInstance(renderData->instance, NULL);
  SDL_free(renderData);
}
