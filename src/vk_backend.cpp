#include "vk_backend.h"
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

VkBackend::VkBackend(GLFWwindow *window) : _window(window) {}

VkBackend::~VkBackend() {}

bool VkBackend::init(Model model) {
  _model = model;
  createInstance();
  setupDebugCallback();
  createSurface();
  pickPhysicalDevice();
  createLogicalDevice();
  createSwapChain();
  createImageViews();
  createGBufferAttachments();
  createRenderPass();
  _gpassPipeline =
      createGraphicsPipeline("shaders/gpass.vert.spv", "shaders/gpass.frag.spv",
                             createGPassDescriptorSetLayout(), 0, 3);
  _lightPipeline =
      createGraphicsPipeline("shaders/light.vert.spv", "shaders/light.frag.spv",
                             createLightDescriptorSetLayout(), 1, 1);
  createCommandPool();
  createDepthResources();
  createFramebuffers();
  // Load textures in GPU memory
  for (auto &mesh : _model.meshes) {
    // TODO: texture cache

    Texture diffuse = createTextureImage(mesh.diffuse_texname);
    createTextureImageView(diffuse);
    createTextureSampler(diffuse);
    _diffuseTextures.push_back(diffuse);

    Texture specular = createTextureImage(mesh.specular_texname);
    createTextureImageView(specular);
    createTextureSampler(specular);
    _specularTextures.push_back(specular);

    Texture normal = createTextureImage(mesh.normal_texname);
    createTextureImageView(normal);
    createTextureSampler(normal);
    _normalTextures.push_back(normal);
  }

  _vertexBuffer = createVertexBuffer(model.vertices);
  _indexBuffer = createIndexBuffer(model.indices);

  _gpassUniformBuffer = createUniformBuffer(sizeof(gPassUbo));
  _lightUniformBuffer = createUniformBuffer(sizeof(lightUbo));

  // Geometry pass descriptor sets
  _gpassPipeline.descriptorPool =
      createGPassDescriptorPool(static_cast<uint32_t>(_model.meshes.size()));
  for (size_t i = 0; i < model.meshes.size(); i++) {
    VkDescriptorSet descriptorSet = createGPassDescriptorSet(
        _gpassPipeline.descriptorPool, _gpassPipeline.descriptorSetLayout,
        _diffuseTextures[i], _specularTextures[i], _normalTextures[i]);
    _gpassPipeline.descriptorSets.push_back(descriptorSet);
  }
  _lightPipeline.descriptorPool = createLightDescriptorPool(1);
  _lightPipeline.descriptorSets.push_back(createLightDescriptorSet(
      _lightPipeline.descriptorPool, _lightPipeline.descriptorSetLayout));
  createCommandBuffers();
  createSemaphores();
  return true;
}

void VkBackend::recreateSwapChain() {
  vkDeviceWaitIdle(_device);

  cleanupSwapChain();

  createSwapChain();
  createImageViews();
  createRenderPass();
  _gpassPipeline =
      createGraphicsPipeline("shaders/vert.spv", "shaders/frag.spv",
                             createGPassDescriptorSetLayout(), 0, 3);
  // createGraphicsPipeline();
  createFramebuffers();
  createCommandBuffers();
}

void VkBackend::drawFrame() {
  uint32_t imageIndex;
  vkQueueWaitIdle(_presentQueue);
  VkResult result = vkAcquireNextImageKHR(
      _device, _swapChain, std::numeric_limits<uint64_t>::max(),
      _imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);
  if (result == VK_ERROR_OUT_OF_DATE_KHR) {
    recreateSwapChain();
    return;
  } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
    throw std::runtime_error("failed to acquire swap chain image!");
  }

  VkSubmitInfo submitInfo = {};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

  VkSemaphore waitSemaphores[] = {_imageAvailableSemaphore};
  VkPipelineStageFlags waitStages[] = {
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
  submitInfo.waitSemaphoreCount = 1;
  submitInfo.pWaitSemaphores = waitSemaphores;
  submitInfo.pWaitDstStageMask = waitStages;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &_commandBuffers[imageIndex];
  VkSemaphore signalSemaphores[] = {_renderFinishedSemaphore};
  submitInfo.signalSemaphoreCount = 1;
  submitInfo.pSignalSemaphores = signalSemaphores;
  result = vkQueueSubmit(_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
  vkCheckResult(result, "vkQueueSubmit");

  VkPresentInfoKHR presentInfo = {};
  presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

  presentInfo.waitSemaphoreCount = 1;
  presentInfo.pWaitSemaphores = signalSemaphores;
  VkSwapchainKHR swapChains[] = {_swapChain};
  presentInfo.swapchainCount = 1;
  presentInfo.pSwapchains = swapChains;
  presentInfo.pImageIndices = &imageIndex;
  presentInfo.pResults = nullptr;
  result = vkQueuePresentKHR(_presentQueue, &presentInfo);
  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
    recreateSwapChain();
  } else if (result != VK_SUCCESS) {
    throw std::runtime_error("failed to present swap chain image!");
  }
}

void VkBackend::update() {
  static auto startTime = std::chrono::high_resolution_clock::now();

  auto currentTime = std::chrono::high_resolution_clock::now();
  float time = std::chrono::duration_cast<std::chrono::milliseconds>(
                   currentTime - startTime)
                   .count() /
               1000.0f;
  gPassUbo gpassUbo = {};
  gpassUbo.model = glm::rotate(glm::mat4(), time * glm::radians(10.0f),
                               glm::vec3(0.0f, 1.0f, 0.0f));
  gpassUbo.view =
      glm::lookAt(glm::vec3(0.0f, 2.0f, 0.0f), glm::vec3(1.0f, 2.0f, 0.0f),
                  glm::vec3(0.0f, 1.0f, 0.0f));
  gpassUbo.proj = glm::perspective(
      glm::radians(45.0f),
      _swapChainExtent.width / (float)_swapChainExtent.height, 0.1f, 100.0f);
  gpassUbo.proj[1][1] *= -1;
  void *data;
  vkMapMemory(_device, _gpassUniformBuffer.bufferMemory, 0, sizeof(gpassUbo), 0,
              &data);
  memcpy(data, &gpassUbo, sizeof(gpassUbo));
  vkUnmapMemory(_device, _gpassUniformBuffer.bufferMemory);

  lightUbo light = {};
  light.viewPosition = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
  for (int i = 0; i < 6; i++) {
    light.lights[i].color = glm::vec3(1.0f, 0.0f, 0.0f);
    light.lights[i].position = glm::vec4(1.0f, 1.0f, 0.0f, 1.0f);
    light.lights[i].radius = 25.0f;
  }
  // White
  light.lights[0].position = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
  light.lights[0].color = glm::vec3(1.5f);
  light.lights[0].radius = 15.0f * 0.25f;
  // Red
  light.lights[1].position = glm::vec4(-2.0f, 0.0f, 0.0f, 0.0f);
  light.lights[1].color = glm::vec3(1.0f, 0.0f, 0.0f);
  light.lights[1].radius = 15.0f;
  // Blue
  light.lights[2].position = glm::vec4(2.0f, 1.0f, 0.0f, 0.0f);
  light.lights[2].color = glm::vec3(0.0f, 0.0f, 2.5f);
  light.lights[2].radius = 5.0f;
  // Yellow
  light.lights[3].position = glm::vec4(0.0f, 0.9f, 0.5f, 0.0f);
  light.lights[3].color = glm::vec3(1.0f, 1.0f, 0.0f);
  light.lights[3].radius = 2.0f;
  // Green
  light.lights[4].position = glm::vec4(0.0f, 0.5f, 0.0f, 0.0f);
  light.lights[4].color = glm::vec3(0.0f, 1.0f, 0.2f);
  light.lights[4].radius = 5.0f;
  // Yellow
  light.lights[5].position = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
  light.lights[5].color = glm::vec3(1.0f, 0.7f, 0.3f);
  light.lights[5].radius = 25.0f;

  light.lights[0].position.x = sin(glm::radians(360.0f * time)) * 5.0f;
  light.lights[0].position.z = cos(glm::radians(360.0f * time)) * 5.0f;

  light.lights[1].position.x =
      -4.0f + sin(glm::radians(360.0f * time) + 45.0f) * 2.0f;
  light.lights[1].position.z =
      0.0f + cos(glm::radians(360.0f * time) + 45.0f) * 2.0f;

  light.lights[2].position.x = 4.0f + sin(glm::radians(360.0f * time)) * 2.0f;
  light.lights[2].position.z = 0.0f + cos(glm::radians(360.0f * time)) * 2.0f;

  light.lights[4].position.x =
      0.0f + sin(glm::radians(360.0f * time + 90.0f)) * 5.0f;
  light.lights[4].position.z =
      0.0f - cos(glm::radians(360.0f * time + 45.0f)) * 5.0f;

  light.lights[5].position.x =
      0.0f + sin(glm::radians(-360.0f * time + 135.0f)) * 10.0f;
  light.lights[5].position.z =
      0.0f - cos(glm::radians(-360.0f * time - 45.0f)) * 10.0f;

  vkMapMemory(_device, _lightUniformBuffer.bufferMemory, 0, sizeof(lightUbo), 0,
              &data);
  memcpy(data, &light, sizeof(lightUbo));
  vkUnmapMemory(_device, _lightUniformBuffer.bufferMemory);
}

void VkBackend::createInstance() {
  VkApplicationInfo appInfo = {};
  appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appInfo.pApplicationName = "VkRenderer";
  appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.pEngineName = "";
  appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.apiVersion = VK_MAKE_VERSION(1, 0, VK_HEADER_VERSION);

  VkInstanceCreateInfo createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  createInfo.pApplicationInfo = &appInfo;

  auto extensions = getRequiredExtensions();
  createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
  createInfo.ppEnabledExtensionNames = extensions.data();
  if (enableValidationLayers && !checkValidationLayerSupport()) {
    createInfo.enabledLayerCount =
        static_cast<uint32_t>(validationLayers.size());
    createInfo.ppEnabledLayerNames = validationLayers.data();
  } else {
    createInfo.enabledLayerCount = 0;
  }

  VkResult result;
  result = vkCreateInstance(&createInfo, nullptr, &_instance);
  vkCheckResult(result, "vkCreateInstance");
}

void VkBackend::setupDebugCallback() {
  if (!enableValidationLayers) return;
  VkDebugReportCallbackCreateInfoEXT createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
  createInfo.flags =
      VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
  createInfo.pfnCallback = debugCallback;
  if (CreateDebugReportCallbackEXT(_instance, &createInfo, nullptr,
                                   &_callback) != VK_SUCCESS) {
    throw std::runtime_error("failed to set up debug callback!");
  }
}

void VkBackend::createSurface() {
  if (glfwCreateWindowSurface(_instance, _window, nullptr, &_surface) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to create window surface!");
  }
}

void VkBackend::pickPhysicalDevice() {
  uint32_t deviceCount = 0;
  VkResult result;
  result = vkEnumeratePhysicalDevices(_instance, &deviceCount, nullptr);
  vkCheckResult(result, "vkEnumeratePhysicalDevices");
  if (deviceCount == 0)
    throw std::runtime_error("failed to find GPUs with Vulkan support!");
  std::vector<VkPhysicalDevice> devices(deviceCount);
  result = vkEnumeratePhysicalDevices(_instance, &deviceCount, devices.data());
  vkCheckResult(result, "vkEnumeratePhysicalDevices");
  std::multimap<int, VkPhysicalDevice> candidates;
  for (const auto &device : devices) {
    int score = rateDeviceSuitability(device, _surface);
    candidates.insert(std::make_pair(score, device));
  }
  if (candidates.rbegin()->first > 0) {
    _physicalDevice = candidates.rbegin()->second;
  }
  if (_physicalDevice == VK_NULL_HANDLE) {
    throw std::runtime_error("failed to find a suitable GPU!");
  }
}

void VkBackend::createLogicalDevice() {
  QueueFamilyIndices indices = findQueueFamilies(_physicalDevice, _surface);
  std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
  std::set<int> uniqueQueueFamilies = {indices.graphicsFamily,
                                       indices.presentFamily};

  float queuePriority = 1.0f;
  for (int queueFamily : uniqueQueueFamilies) {
    VkDeviceQueueCreateInfo queueCreateInfo = {};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = queueFamily;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;
    queueCreateInfos.push_back(queueCreateInfo);
  }

  VkPhysicalDeviceFeatures deviceFeatures = {};
  deviceFeatures.samplerAnisotropy = VK_TRUE;

  VkDeviceCreateInfo createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;

  createInfo.queueCreateInfoCount =
      static_cast<uint32_t>(queueCreateInfos.size());
  createInfo.pQueueCreateInfos = queueCreateInfos.data();

  createInfo.pEnabledFeatures = &deviceFeatures;

  createInfo.enabledExtensionCount =
      static_cast<uint32_t>(deviceExtensions.size());
  createInfo.ppEnabledExtensionNames = deviceExtensions.data();

  if (enableValidationLayers) {
    createInfo.enabledLayerCount =
        static_cast<uint32_t>(validationLayers.size());
    createInfo.ppEnabledLayerNames = validationLayers.data();
  } else {
    createInfo.enabledLayerCount = 0;
  }
  VkResult result;
  result = vkCreateDevice(_physicalDevice, &createInfo, nullptr, &_device);
  vkCheckResult(result, "vkCreateDevice");
  vkGetDeviceQueue(_device, indices.graphicsFamily, 0, &_graphicsQueue);
  vkGetDeviceQueue(_device, indices.presentFamily, 0, &_presentQueue);
}

void VkBackend::createSwapChain() {
  SwapChainSupportDetails swapChainSupport =
      querySwapChainSupport(_physicalDevice, _surface);

  VkSurfaceFormatKHR surfaceFormat =
      chooseSwapSurfaceFormat(swapChainSupport.formats);
  VkPresentModeKHR presentMode =
      chooseSwapPresentMode(swapChainSupport.presentModes);
  VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities);

  uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
  if (swapChainSupport.capabilities.maxImageCount > 0 &&
      imageCount > swapChainSupport.capabilities.maxImageCount) {
    imageCount = swapChainSupport.capabilities.maxImageCount;
  }
  VkSwapchainCreateInfoKHR createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  createInfo.surface = _surface;
  createInfo.minImageCount = imageCount;
  createInfo.imageFormat = surfaceFormat.format;
  createInfo.imageColorSpace = surfaceFormat.colorSpace;
  createInfo.imageExtent = extent;
  createInfo.imageArrayLayers = 1;
  createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  QueueFamilyIndices indices = findQueueFamilies(_physicalDevice, _surface);
  if (indices.graphicsFamily != indices.presentFamily) {
    uint32_t queueFamilyIndices[] = {(uint32_t)indices.graphicsFamily,
                                     (uint32_t)indices.presentFamily};
    createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
    createInfo.queueFamilyIndexCount = 2;
    createInfo.pQueueFamilyIndices = queueFamilyIndices;
  } else {
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.queueFamilyIndexCount = 0;      // Optional
    createInfo.pQueueFamilyIndices = nullptr;  // Optional
  }
  createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
  createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  createInfo.presentMode = presentMode;
  createInfo.clipped = VK_TRUE;
  createInfo.oldSwapchain = VK_NULL_HANDLE;
  VkResult result;
  result = vkCreateSwapchainKHR(_device, &createInfo, nullptr, &_swapChain);
  vkCheckResult(result, "vkCreateSwapchainKHR");

  result = vkGetSwapchainImagesKHR(_device, _swapChain, &imageCount, nullptr);
  vkCheckResult(result, "vkGetSwapchainImagesKHR");
  _swapChainImages.resize(imageCount);
  result = vkGetSwapchainImagesKHR(_device, _swapChain, &imageCount,
                                   _swapChainImages.data());
  vkCheckResult(result, "vkGetSwapchainImagesKHR");

  _swapChainImageFormat = surfaceFormat.format;
  _swapChainExtent = extent;
}

void VkBackend::createImageViews() {
  _swapChainImageViews.resize(_swapChainImages.size());
  for (size_t i = 0; i < _swapChainImages.size(); i++) {
    _swapChainImageViews[i] = createImageView(
        _swapChainImages[i], _swapChainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT);
  }
}

void VkBackend::createRenderPass() {
  std::array<VkAttachmentDescription, 5> attachments{};
  attachments[0].format = _swapChainImageFormat;
  attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;  // must match swap chain
                                                   // image
  attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  attachments[1].format = _gBufferAttachments[0].format;
  attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
  attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  attachments[1].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  attachments[2].format = _gBufferAttachments[1].format;
  attachments[2].samples = VK_SAMPLE_COUNT_1_BIT;
  attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  attachments[2].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  attachments[3].format = _gBufferAttachments[2].format;
  attachments[3].samples = VK_SAMPLE_COUNT_1_BIT;
  attachments[3].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  attachments[3].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[3].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachments[3].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[3].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  attachments[3].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  attachments[4].format = findDepthFormat(_physicalDevice);
  attachments[4].samples = VK_SAMPLE_COUNT_1_BIT;
  attachments[4].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  attachments[4].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[4].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachments[4].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[4].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  attachments[4].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  std::array<VkSubpassDescription, 2> subpasses{};

  std::array<VkAttachmentReference, 3> firstSubpassColors;
  firstSubpassColors[0] = {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  firstSubpassColors[1] = {2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  firstSubpassColors[2] = {3, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

  VkAttachmentReference firstSubpassDepth = {
      4, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

  subpasses[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpasses[0].colorAttachmentCount =
      static_cast<uint32_t>(firstSubpassColors.size());
  subpasses[0].pColorAttachments = firstSubpassColors.data();
  subpasses[0].pDepthStencilAttachment = &firstSubpassDepth;

  VkAttachmentReference secondSubpassColor = {
      0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  std::array<VkAttachmentReference, 3> secondSubpassInput;
  secondSubpassInput[0] = {1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  secondSubpassInput[1] = {2, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  secondSubpassInput[2] = {3, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  VkAttachmentReference secondSubpassDepth = {
      4, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

  subpasses[1].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpasses[1].colorAttachmentCount = 1;
  subpasses[1].pColorAttachments = &secondSubpassColor;
  subpasses[1].inputAttachmentCount =
      static_cast<uint32_t>(secondSubpassInput.size());
  subpasses[1].pInputAttachments = secondSubpassInput.data();
  subpasses[1].pDepthStencilAttachment = &secondSubpassDepth;

  std::array<VkSubpassDependency, 3> dependencies;

  dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
  dependencies[0].dstSubpass = 0;
  dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
  dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
  dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

  dependencies[1].srcSubpass = 0;
  dependencies[1].dstSubpass = 1;
  dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

  dependencies[2].srcSubpass = 0;
  dependencies[2].dstSubpass = VK_SUBPASS_EXTERNAL;
  dependencies[2].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependencies[2].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
  dependencies[2].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  dependencies[2].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
  dependencies[2].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

  VkRenderPassCreateInfo renderPassInfo = {};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
  renderPassInfo.pAttachments = attachments.data();
  renderPassInfo.subpassCount = static_cast<uint32_t>(subpasses.size());
  renderPassInfo.pSubpasses = subpasses.data();
  renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
  renderPassInfo.pDependencies = dependencies.data();

  VkResult result =
      vkCreateRenderPass(_device, &renderPassInfo, nullptr, &_renderPass);
  vkCheckResult(result, "vkCreateRenderPass");
}

VkDescriptorSetLayout VkBackend::createGPassDescriptorSetLayout() {
  VkDescriptorSetLayout descriptorSetLayout = {};
  VkDescriptorSetLayoutBinding uboLayoutBinding = {};
  uboLayoutBinding.binding = 0;
  uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  uboLayoutBinding.descriptorCount = 1;
  uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  uboLayoutBinding.pImmutableSamplers = nullptr;

  VkDescriptorSetLayoutBinding ambientSamplerLayoutBinding = {};
  ambientSamplerLayoutBinding.binding = 1;
  ambientSamplerLayoutBinding.descriptorCount = 1;
  ambientSamplerLayoutBinding.descriptorType =
      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  ambientSamplerLayoutBinding.pImmutableSamplers = nullptr;
  ambientSamplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  VkDescriptorSetLayoutBinding diffuseSamplerLayoutBinding = {};
  diffuseSamplerLayoutBinding.binding = 2;
  diffuseSamplerLayoutBinding.descriptorCount = 1;
  diffuseSamplerLayoutBinding.descriptorType =
      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  diffuseSamplerLayoutBinding.pImmutableSamplers = nullptr;
  diffuseSamplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  VkDescriptorSetLayoutBinding specularSamplerLayoutBinding = {};
  specularSamplerLayoutBinding.binding = 3;
  specularSamplerLayoutBinding.descriptorCount = 1;
  specularSamplerLayoutBinding.descriptorType =
      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  specularSamplerLayoutBinding.pImmutableSamplers = nullptr;
  specularSamplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  std::array<VkDescriptorSetLayoutBinding, 4> bindings = {
      uboLayoutBinding, ambientSamplerLayoutBinding,
      diffuseSamplerLayoutBinding, specularSamplerLayoutBinding};
  VkDescriptorSetLayoutCreateInfo layoutInfo = {};
  layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
  layoutInfo.pBindings = bindings.data();

  VkResult result = vkCreateDescriptorSetLayout(_device, &layoutInfo, nullptr,
                                                &descriptorSetLayout);
  vkCheckResult(result, "vkCreateDescriptorSetLayout");
  return descriptorSetLayout;
}

VkDescriptorSetLayout VkBackend::createLightDescriptorSetLayout() {
  VkDescriptorSetLayout descriptorSetLayout = {};

  VkDescriptorSetLayoutBinding positionInputLayoutBinding = {};
  positionInputLayoutBinding.binding = 0;
  positionInputLayoutBinding.descriptorCount = 1;
  positionInputLayoutBinding.descriptorType =
      VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
  positionInputLayoutBinding.pImmutableSamplers = nullptr;
  positionInputLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  VkDescriptorSetLayoutBinding normalInputLayoutBinding = {};
  normalInputLayoutBinding.binding = 1;
  normalInputLayoutBinding.descriptorCount = 1;
  normalInputLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
  normalInputLayoutBinding.pImmutableSamplers = nullptr;
  normalInputLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  VkDescriptorSetLayoutBinding albedoInputLayoutBinding = {};
  albedoInputLayoutBinding.binding = 2;
  albedoInputLayoutBinding.descriptorCount = 1;
  albedoInputLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
  albedoInputLayoutBinding.pImmutableSamplers = nullptr;
  albedoInputLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  VkDescriptorSetLayoutBinding uboLayoutBinding = {};
  uboLayoutBinding.binding = 3;
  uboLayoutBinding.descriptorCount = 1;
  uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  uboLayoutBinding.pImmutableSamplers = nullptr;
  uboLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  std::array<VkDescriptorSetLayoutBinding, 4> bindings = {
      positionInputLayoutBinding, normalInputLayoutBinding,
      albedoInputLayoutBinding, uboLayoutBinding};

  VkDescriptorSetLayoutCreateInfo layoutInfo = {};
  layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
  layoutInfo.pBindings = bindings.data();

  VkResult result = vkCreateDescriptorSetLayout(_device, &layoutInfo, nullptr,
                                                &descriptorSetLayout);
  vkCheckResult(result, "vkCreateDescriptorSetLayout");
  return descriptorSetLayout;
}

Pipeline VkBackend::createGraphicsPipeline(
    const std::string vertexShader, const std::string fragShader,
    VkDescriptorSetLayout descriptorSetLayout, uint32_t subpass_id,
    uint32_t colorAttachmentCount) {
  Pipeline pipeline = {};  // TODO: give pipeline his own class
  pipeline.descriptorSetLayout = descriptorSetLayout;

  auto vertShaderCode = readShader(vertexShader);
  auto fragShaderCode = readShader(fragShader);
  VkShaderModule vertShaderModule = createShaderModule(_device, vertShaderCode);
  VkShaderModule fragShaderModule = createShaderModule(_device, fragShaderCode);

  VkPipelineShaderStageCreateInfo vertShaderStageInfo = {};
  vertShaderStageInfo.sType =
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
  vertShaderStageInfo.module = vertShaderModule;
  vertShaderStageInfo.pName = "main";

  VkPipelineShaderStageCreateInfo fragShaderStageInfo = {};
  fragShaderStageInfo.sType =
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  fragShaderStageInfo.module = fragShaderModule;
  fragShaderStageInfo.pName = "main";

  VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo,
                                                    fragShaderStageInfo};

  auto bindingDescription = VkVertex::getBindingDescription();
  auto attributeDescriptions = VkVertex::getAttributeDescriptions();

  VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
  vertexInputInfo.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInputInfo.vertexBindingDescriptionCount = 1;
  vertexInputInfo.vertexAttributeDescriptionCount =
      static_cast<uint32_t>(attributeDescriptions.size());
  vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
  vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

  VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
  inputAssembly.sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  inputAssembly.primitiveRestartEnable = VK_FALSE;

  VkViewport viewport = {};
  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.width = (float)_swapChainExtent.width;
  viewport.height = (float)_swapChainExtent.height;
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;

  VkRect2D scissor = {};
  scissor.offset = {0, 0};
  scissor.extent = _swapChainExtent;

  VkPipelineViewportStateCreateInfo viewportState = {};
  viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportState.viewportCount = 1;
  viewportState.pViewports = &viewport;
  viewportState.scissorCount = 1;
  viewportState.pScissors = &scissor;

  VkPipelineRasterizationStateCreateInfo rasterizer = {};
  rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizer.depthClampEnable = VK_FALSE;
  rasterizer.rasterizerDiscardEnable = VK_FALSE;
  rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizer.cullMode = VK_CULL_MODE_NONE;
  rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rasterizer.depthBiasEnable = VK_FALSE;
  rasterizer.depthBiasConstantFactor = 0.0f;
  rasterizer.depthBiasClamp = 0.0f;
  rasterizer.depthBiasSlopeFactor = 0.0f;
  rasterizer.lineWidth =
      1.0f;  // Not used but produce validation error when not set

  VkPipelineMultisampleStateCreateInfo multisampling = {};
  multisampling.sType =
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampling.sampleShadingEnable = VK_FALSE;
  multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  multisampling.minSampleShading = 1.0f;
  multisampling.pSampleMask = nullptr;
  multisampling.alphaToCoverageEnable = VK_FALSE;
  multisampling.alphaToOneEnable = VK_FALSE;

  std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachmentStates;
  for (size_t i = 0; i < colorAttachmentCount; i++) {
    VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
    colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachmentStates.push_back(colorBlendAttachment);
  }

  VkPipelineColorBlendStateCreateInfo colorBlending = {};
  colorBlending.sType =
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlending.logicOpEnable = VK_FALSE;
  colorBlending.logicOp = VK_LOGIC_OP_COPY;
  colorBlending.attachmentCount =
      static_cast<uint32_t>(colorBlendAttachmentStates.size());
  colorBlending.pAttachments = colorBlendAttachmentStates.data();
  colorBlending.blendConstants[0] = 0.0f;
  colorBlending.blendConstants[1] = 0.0f;
  colorBlending.blendConstants[2] = 0.0f;
  colorBlending.blendConstants[3] = 0.0f;

  VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                    VK_DYNAMIC_STATE_LINE_WIDTH};

  VkPipelineDynamicStateCreateInfo dynamicState = {};
  dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamicState.dynamicStateCount = 2;
  dynamicState.pDynamicStates = dynamicStates;

  VkPipelineDepthStencilStateCreateInfo depthStencil = {};
  depthStencil.sType =
      VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depthStencil.depthTestEnable = VK_TRUE;
  depthStencil.depthWriteEnable = VK_TRUE;
  depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
  depthStencil.depthBoundsTestEnable = VK_FALSE;
  depthStencil.minDepthBounds = 0.0f;
  depthStencil.maxDepthBounds = 1.0f;
  depthStencil.stencilTestEnable = VK_FALSE;
  depthStencil.front = {};
  depthStencil.back = {};

  VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = 1;
  pipelineLayoutInfo.pSetLayouts = &pipeline.descriptorSetLayout;
  pipelineLayoutInfo.pushConstantRangeCount = 0;
  pipelineLayoutInfo.pPushConstantRanges = 0;

  VkResult result = vkCreatePipelineLayout(_device, &pipelineLayoutInfo,
                                           nullptr, &pipeline.layout);
  vkCheckResult(result, "vkCreatePipelineLayout");

  VkGraphicsPipelineCreateInfo pipelineInfo = {};
  pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipelineInfo.stageCount = 2;
  pipelineInfo.pStages = shaderStages;
  pipelineInfo.pVertexInputState = &vertexInputInfo;
  pipelineInfo.pInputAssemblyState = &inputAssembly;
  pipelineInfo.pViewportState = &viewportState;
  pipelineInfo.pRasterizationState = &rasterizer;
  pipelineInfo.pMultisampleState = &multisampling;
  pipelineInfo.pDepthStencilState = &depthStencil;
  pipelineInfo.pColorBlendState = &colorBlending;
  pipelineInfo.pDynamicState = nullptr;

  pipelineInfo.layout = pipeline.layout;
  pipelineInfo.renderPass = _renderPass;
  pipelineInfo.subpass = subpass_id;
  pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
  pipelineInfo.basePipelineIndex = -1;

  result = vkCreateGraphicsPipelines(_device, VK_NULL_HANDLE, 1, &pipelineInfo,
                                     nullptr, &pipeline.pipeline);
  vkCheckResult(result, "vkCreateGraphicsPipelines");

  vkDestroyShaderModule(_device, fragShaderModule, nullptr);
  vkDestroyShaderModule(_device, vertShaderModule, nullptr);
  return pipeline;
}

void VkBackend::createFramebuffers() {
  _swapChainFramebuffers.resize(_swapChainImageViews.size());
  for (size_t i = 0; i < _swapChainImageViews.size(); i++) {
    std::array<VkImageView, 5> attachments = {
        _swapChainImageViews[i],
        _gBufferAttachments[0].imageView,  // positions
        _gBufferAttachments[1].imageView,  // normals
        _gBufferAttachments[2].imageView,  // albedo
        _depth.imageView};

    VkFramebufferCreateInfo framebufferInfo = {};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = _renderPass;
    framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    framebufferInfo.pAttachments = attachments.data();
    framebufferInfo.width = _swapChainExtent.width;
    framebufferInfo.height = _swapChainExtent.height;
    framebufferInfo.layers = 1;

    VkResult result = vkCreateFramebuffer(_device, &framebufferInfo, nullptr,
                                          &_swapChainFramebuffers[i]);
    vkCheckResult(result, "vkCreateFramebuffer");
  }
}

void VkBackend::createCommandPool() {
  QueueFamilyIndices queueFamilyIndices =
      findQueueFamilies(_physicalDevice, _surface);

  VkCommandPoolCreateInfo poolInfo = {};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily;
  poolInfo.flags = 0;
  VkResult result =
      vkCreateCommandPool(_device, &poolInfo, nullptr, &_commandPool);
  vkCheckResult(result, "vkCreateCommandPool");
}

void VkBackend::createDepthResources() {
  VkFormat depthFormat = findDepthFormat(_physicalDevice);
  createImage(
      _swapChainExtent.width, _swapChainExtent.height, depthFormat,
      VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, _depth.image, _depth.imageMemory);
  _depth.imageView =
      createImageView(_depth.image, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);
  transitionImageLayout(_depth.image, depthFormat, VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
}

void VkBackend::createGBufferAttachments() {
  Attachment position = {};
  position.format = VK_FORMAT_R16G16B16A16_SFLOAT;
  createImage(
      _swapChainExtent.width, _swapChainExtent.height, position.format,
      VK_IMAGE_TILING_OPTIMAL,
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, position.image, position.memory);
  position.imageView = createImageView(position.image, position.format,
                                       VK_IMAGE_ASPECT_COLOR_BIT);

  Attachment normal = {};
  normal.format = VK_FORMAT_R16G16B16A16_SFLOAT;
  createImage(
      _swapChainExtent.width, _swapChainExtent.height, normal.format,
      VK_IMAGE_TILING_OPTIMAL,
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, normal.image, normal.memory);
  normal.imageView =
      createImageView(normal.image, normal.format, VK_IMAGE_ASPECT_COLOR_BIT);

  Attachment albedo = {};
  // albedo.format = VK_FORMAT_R8G8B8A8_UNORM;
  albedo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
  createImage(
      _swapChainExtent.width, _swapChainExtent.height, albedo.format,
      VK_IMAGE_TILING_OPTIMAL,
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, albedo.image, albedo.memory);
  albedo.imageView =
      createImageView(albedo.image, albedo.format, VK_IMAGE_ASPECT_COLOR_BIT);

  _gBufferAttachments.push_back(position);
  _gBufferAttachments.push_back(normal);
  _gBufferAttachments.push_back(albedo);
}

Texture VkBackend::createTextureImage(const std::string filepath) {
  Texture texture = {};
  VkBuffer stagingBuffer;
  VkDeviceMemory stagingBufferMemory;
  int texWidth, texHeight, texChannels;
  stbi_uc *pixels = stbi_load(filepath.c_str(), &texWidth, &texHeight,
                              &texChannels, STBI_rgb_alpha);
  if (!pixels) {
    stbi_image_free(pixels);
    uint8_t pixels[4];  // No texture or can't load it, using a dummy texture
                        // instead
    pixels[0] = 255;    // AFAIK you can't have null texture in Vulkan
    pixels[1] = 255;
    pixels[2] = 255;
    pixels[3] = 255;

    texWidth = 1, texHeight = 1;

    VkDeviceSize imageSize = texWidth * texHeight * 4;
    createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 stagingBuffer, stagingBufferMemory);
    void *data;
    vkMapMemory(_device, stagingBufferMemory, 0, imageSize, 0, &data);
    memcpy(data, pixels, static_cast<size_t>(imageSize));
    vkUnmapMemory(_device, stagingBufferMemory);
  } else {
    VkDeviceSize imageSize = texWidth * texHeight * 4;
    createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 stagingBuffer, stagingBufferMemory);
    void *data;
    vkMapMemory(_device, stagingBufferMemory, 0, imageSize, 0, &data);
    memcpy(data, pixels, static_cast<size_t>(imageSize));
    vkUnmapMemory(_device, stagingBufferMemory);
    stbi_image_free(pixels);
  }

  createImage(
      texWidth, texHeight, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, texture.image, texture.imageMemory);
  transitionImageLayout(texture.image, VK_FORMAT_R8G8B8A8_UNORM,
                        VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  copyBufferToImage(stagingBuffer, texture.image,
                    static_cast<uint32_t>(texWidth),
                    static_cast<uint32_t>(texHeight));
  transitionImageLayout(texture.image, VK_FORMAT_R8G8B8A8_UNORM,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  vkDestroyBuffer(_device, stagingBuffer, nullptr);
  vkFreeMemory(_device, stagingBufferMemory, nullptr);
  return texture;
}

VkImageView VkBackend::createImageView(VkImage image, VkFormat format,
                                       VkImageAspectFlags aspectFlags) {
  VkImageViewCreateInfo viewInfo = {};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = image;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = format;
  viewInfo.subresourceRange.aspectMask = aspectFlags;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  VkImageView imageView;
  VkResult result = vkCreateImageView(_device, &viewInfo, nullptr, &imageView);
  vkCheckResult(result, "vkCreateImageView");

  return imageView;
}

void VkBackend::createTextureImageView(Texture &texture) {
  if (texture.image != VK_NULL_HANDLE) {
    texture.imageView = createImageView(texture.image, VK_FORMAT_R8G8B8A8_UNORM,
                                        VK_IMAGE_ASPECT_COLOR_BIT);
  }
}

void VkBackend::createTextureSampler(Texture &texture) {
  VkSamplerCreateInfo samplerInfo = {};
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.magFilter = VK_FILTER_LINEAR;
  samplerInfo.minFilter = VK_FILTER_LINEAR;
  samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.anisotropyEnable = VK_TRUE;
  samplerInfo.maxAnisotropy = 16;
  samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  samplerInfo.unnormalizedCoordinates = VK_FALSE;
  samplerInfo.compareEnable = VK_FALSE;
  samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  samplerInfo.mipLodBias = 0.0f;
  samplerInfo.minLod = 0.0f;
  samplerInfo.maxLod = 0.0f;

  VkResult result =
      vkCreateSampler(_device, &samplerInfo, nullptr, &texture.sampler);
  vkCheckResult(result, "vkCreateSampler");
}

void VkBackend::createImage(uint32_t width, uint32_t height, VkFormat format,
                            VkImageTiling tiling, VkImageUsageFlags usage,
                            VkMemoryPropertyFlags properties, VkImage &image,
                            VkDeviceMemory &imageMemory) {
  VkImageCreateInfo imageInfo = {};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent.width = width;
  imageInfo.extent.height = height;
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.format = format;
  imageInfo.tiling = tiling;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.usage = usage;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  VkResult result = vkCreateImage(_device, &imageInfo, nullptr, &image);
  vkCheckResult(result, "vkCreateImage");

  VkMemoryRequirements memRequirements;
  vkGetImageMemoryRequirements(_device, image, &memRequirements);

  VkMemoryAllocateInfo allocInfo = {};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex = findMemoryType(
      _physicalDevice, memRequirements.memoryTypeBits, properties);

  result = vkAllocateMemory(_device, &allocInfo, nullptr, &imageMemory);
  vkCheckResult(result, "vkAllocateMemory");

  vkBindImageMemory(_device, image, imageMemory, 0);
}

Buffer VkBackend::createVertexBuffer(std::vector<Vertex> vertices) {
  Buffer vertex;
  VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();
  VkBuffer stagingBuffer;
  VkDeviceMemory stagingBufferMemory;
  createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
               stagingBuffer, stagingBufferMemory);

  void *data;
  vkMapMemory(_device, stagingBufferMemory, 0, bufferSize, 0, &data);
  memcpy(data, vertices.data(), (size_t)bufferSize);
  vkUnmapMemory(_device, stagingBufferMemory);

  createBuffer(
      bufferSize,
      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vertex.buffer, vertex.bufferMemory);
  copyBuffer(stagingBuffer, vertex.buffer, bufferSize);
  vkDestroyBuffer(_device, stagingBuffer, nullptr);
  vkFreeMemory(_device, stagingBufferMemory, nullptr);
  return vertex;
}

Buffer VkBackend::createIndexBuffer(std::vector<uint32_t> indices) {
  Buffer index;
  VkDeviceSize bufferSize = sizeof(indices[0]) * indices.size();

  VkBuffer stagingBuffer;
  VkDeviceMemory stagingBufferMemory;
  createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
               stagingBuffer, stagingBufferMemory);

  void *data;
  vkMapMemory(_device, stagingBufferMemory, 0, bufferSize, 0, &data);
  memcpy(data, indices.data(), (size_t)bufferSize);
  vkUnmapMemory(_device, stagingBufferMemory);

  createBuffer(
      bufferSize,
      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, index.buffer, index.bufferMemory);

  copyBuffer(stagingBuffer, index.buffer, bufferSize);

  vkDestroyBuffer(_device, stagingBuffer, nullptr);
  vkFreeMemory(_device, stagingBufferMemory, nullptr);
  return index;
}

Buffer VkBackend::createUniformBuffer(size_t size) {
  Buffer buffer;
  VkDeviceSize bufferSize = size;
  createBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
               buffer.buffer, buffer.bufferMemory);
  return buffer;
}

VkDescriptorPool VkBackend::createGPassDescriptorPool(uint32_t poolSize) {
  VkDescriptorPool descriptorPool = {};
  std::array<VkDescriptorPoolSize, 4> poolSizes = {};
  poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  poolSizes[0].descriptorCount = poolSize;

  poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  poolSizes[1].descriptorCount = poolSize;

  poolSizes[2].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  poolSizes[2].descriptorCount = poolSize;

  poolSizes[3].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  poolSizes[3].descriptorCount = poolSize;

  VkDescriptorPoolCreateInfo poolInfo = {};
  poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
  poolInfo.pPoolSizes = poolSizes.data();
  poolInfo.maxSets = poolSize;

  VkResult result =
      vkCreateDescriptorPool(_device, &poolInfo, nullptr, &descriptorPool);
  vkCheckResult(result, "vkCreateDescriptorPool");
  return descriptorPool;
}

VkDescriptorSet VkBackend::createGPassDescriptorSet(
    VkDescriptorPool descriptorPool, VkDescriptorSetLayout descriptorSetLayout,
    const Texture &diffuse, const Texture &specular, const Texture &normal) {
  VkDescriptorSet descriptorSet;
  VkDescriptorSetLayout layouts[] = {descriptorSetLayout};

  VkDescriptorSetAllocateInfo allocInfo = {};
  allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocInfo.descriptorPool = descriptorPool;
  allocInfo.descriptorSetCount = 1;
  allocInfo.pSetLayouts = layouts;

  VkResult result =
      vkAllocateDescriptorSets(_device, &allocInfo, &descriptorSet);
  vkCheckResult(result, "vkAllocateDescriptorSets");

  VkDescriptorBufferInfo bufferInfo = {};
  bufferInfo.buffer = _gpassUniformBuffer.buffer;
  bufferInfo.offset = 0;
  bufferInfo.range = sizeof(gPassUbo);

  VkDescriptorImageInfo imageInfo1 = {};
  imageInfo1.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  imageInfo1.imageView = diffuse.imageView;
  imageInfo1.sampler = diffuse.sampler;

  VkDescriptorImageInfo imageInfo2 = {};
  imageInfo2.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  imageInfo2.imageView = specular.imageView;
  imageInfo2.sampler = specular.sampler;

  VkDescriptorImageInfo imageInfo3 = {};
  imageInfo3.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  imageInfo3.imageView = normal.imageView;
  imageInfo3.sampler = normal.sampler;

  std::array<VkWriteDescriptorSet, 4> descriptorWrites = {};

  descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  descriptorWrites[0].dstSet = descriptorSet;
  descriptorWrites[0].dstBinding = 0;
  descriptorWrites[0].dstArrayElement = 0;
  descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  descriptorWrites[0].descriptorCount = 1;
  descriptorWrites[0].pBufferInfo = &bufferInfo;

  descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  descriptorWrites[1].dstSet = descriptorSet;
  descriptorWrites[1].dstBinding = 1;
  descriptorWrites[1].dstArrayElement = 0;
  descriptorWrites[1].descriptorType =
      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  descriptorWrites[1].descriptorCount = 1;
  descriptorWrites[1].pImageInfo = &imageInfo1;

  descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  descriptorWrites[2].dstSet = descriptorSet;
  descriptorWrites[2].dstBinding = 2;
  descriptorWrites[2].dstArrayElement = 0;
  descriptorWrites[2].descriptorType =
      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  descriptorWrites[2].descriptorCount = 1;
  descriptorWrites[2].pImageInfo = &imageInfo2;

  descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  descriptorWrites[3].dstSet = descriptorSet;
  descriptorWrites[3].dstBinding = 3;
  descriptorWrites[3].dstArrayElement = 0;
  descriptorWrites[3].descriptorType =
      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  descriptorWrites[3].descriptorCount = 1;
  descriptorWrites[3].pImageInfo = &imageInfo3;
  vkUpdateDescriptorSets(_device,
                         static_cast<uint32_t>(descriptorWrites.size()),
                         descriptorWrites.data(), 0, nullptr);
  return descriptorSet;
}

VkDescriptorPool VkBackend::createLightDescriptorPool(uint32_t poolSize) {
  VkDescriptorPool descriptorPool = {};
  std::array<VkDescriptorPoolSize, 4> poolSizes = {};
  poolSizes[0].type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
  poolSizes[0].descriptorCount = 1;

  poolSizes[1].type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
  poolSizes[1].descriptorCount = 1;

  poolSizes[2].type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
  poolSizes[2].descriptorCount = 1;

  poolSizes[3].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  poolSizes[3].descriptorCount = 1;

  VkDescriptorPoolCreateInfo poolInfo = {};
  poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
  poolInfo.pPoolSizes = poolSizes.data();
  poolInfo.maxSets = 1;

  VkResult result =
      vkCreateDescriptorPool(_device, &poolInfo, nullptr, &descriptorPool);
  vkCheckResult(result, "vkCreateDescriptorPool");
  return descriptorPool;
}

VkDescriptorSet VkBackend::createLightDescriptorSet(
    VkDescriptorPool descriptorPool,
    VkDescriptorSetLayout descriptorSetLayout) {
  VkDescriptorSet descriptorSet;
  VkDescriptorSetLayout layouts[] = {descriptorSetLayout};

  VkDescriptorSetAllocateInfo allocInfo = {};
  allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocInfo.descriptorPool = descriptorPool;
  allocInfo.descriptorSetCount = 1;
  allocInfo.pSetLayouts = layouts;

  VkResult result =
      vkAllocateDescriptorSets(_device, &allocInfo, &descriptorSet);
  vkCheckResult(result, "vkAllocateDescriptorSets");

  VkDescriptorImageInfo inputInfo1 = {};
  inputInfo1.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  inputInfo1.imageView = _gBufferAttachments[0].imageView;

  VkDescriptorImageInfo inputInfo2 = {};
  inputInfo2.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  inputInfo2.imageView = _gBufferAttachments[1].imageView;

  VkDescriptorImageInfo inputInfo3 = {};
  inputInfo3.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  inputInfo3.imageView = _gBufferAttachments[2].imageView;

  VkDescriptorBufferInfo uboInfo = {};
  uboInfo.buffer = _lightUniformBuffer.buffer;
  uboInfo.offset = 0;
  uboInfo.range = sizeof(lightUbo);

  std::array<VkWriteDescriptorSet, 4> descriptorWrites = {};

  descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  descriptorWrites[0].dstSet = descriptorSet;
  descriptorWrites[0].dstBinding = 0;
  descriptorWrites[0].dstArrayElement = 0;
  descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
  descriptorWrites[0].descriptorCount = 1;
  descriptorWrites[0].pImageInfo = &inputInfo1;

  descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  descriptorWrites[1].dstSet = descriptorSet;
  descriptorWrites[1].dstBinding = 1;
  descriptorWrites[1].dstArrayElement = 0;
  descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
  descriptorWrites[1].descriptorCount = 1;
  descriptorWrites[1].pImageInfo = &inputInfo2;

  descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  descriptorWrites[2].dstSet = descriptorSet;
  descriptorWrites[2].dstBinding = 2;
  descriptorWrites[2].dstArrayElement = 0;
  descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
  descriptorWrites[2].descriptorCount = 1;
  descriptorWrites[2].pImageInfo = &inputInfo3;

  descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  descriptorWrites[3].dstSet = descriptorSet;
  descriptorWrites[3].dstBinding = 3;
  descriptorWrites[3].dstArrayElement = 0;
  descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  descriptorWrites[3].descriptorCount = 1;
  descriptorWrites[3].pBufferInfo = &uboInfo;

  vkUpdateDescriptorSets(_device,
                         static_cast<uint32_t>(descriptorWrites.size()),
                         descriptorWrites.data(), 0, nullptr);
  return descriptorSet;
}

void VkBackend::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                             VkMemoryPropertyFlags properties, VkBuffer &buffer,
                             VkDeviceMemory &bufferMemory) {
  VkBufferCreateInfo bufferInfo = {};
  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size = size;
  bufferInfo.usage = usage;
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VkResult result = vkCreateBuffer(_device, &bufferInfo, nullptr, &buffer);
  vkCheckResult(result, "vkCreateBuffer");

  VkMemoryRequirements memRequirements;
  vkGetBufferMemoryRequirements(_device, buffer, &memRequirements);

  VkMemoryAllocateInfo allocInfo = {};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex = findMemoryType(
      _physicalDevice, memRequirements.memoryTypeBits, properties);

  vkAllocateMemory(_device, &allocInfo, nullptr, &bufferMemory);
  vkCheckResult(result, "vkAllocateMemory");

  vkBindBufferMemory(_device, buffer, bufferMemory, 0);
}

void VkBackend::copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer,
                           VkDeviceSize size) {
  VkCommandBuffer commandBuffer = beginSingleTimeCommands();

  VkBufferCopy copyRegion = {};
  copyRegion.size = size;
  vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

  endSingleTimeCommands(commandBuffer);
}

VkCommandBuffer VkBackend::beginSingleTimeCommands() {
  VkCommandBufferAllocateInfo allocInfo = {};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandPool = _commandPool;
  allocInfo.commandBufferCount = 1;

  VkCommandBuffer commandBuffer;
  vkAllocateCommandBuffers(_device, &allocInfo, &commandBuffer);

  VkCommandBufferBeginInfo beginInfo = {};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  vkBeginCommandBuffer(commandBuffer, &beginInfo);

  return commandBuffer;
}

void VkBackend::endSingleTimeCommands(VkCommandBuffer commandBuffer) {
  vkEndCommandBuffer(commandBuffer);

  VkSubmitInfo submitInfo = {};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffer;

  vkQueueSubmit(_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
  vkQueueWaitIdle(_graphicsQueue);

  vkFreeCommandBuffers(_device, _commandPool, 1, &commandBuffer);
}

void VkBackend::transitionImageLayout(VkImage image, VkFormat format,
                                      VkImageLayout oldLayout,
                                      VkImageLayout newLayout) {
  VkCommandBuffer commandBuffer = beginSingleTimeCommands();

  VkImageMemoryBarrier barrier = {};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = oldLayout;
  barrier.newLayout = newLayout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (hasStencilComponent(format)) {
      barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
  } else {
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  }
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;

  if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
      newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
             newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  } else {
    throw std::invalid_argument("unsupported layout transition!");
  }
  vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                       VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &barrier);
  endSingleTimeCommands(commandBuffer);
}

void VkBackend::copyBufferToImage(VkBuffer buffer, VkImage image,
                                  uint32_t width, uint32_t height) {
  VkCommandBuffer commandBuffer = beginSingleTimeCommands();

  VkBufferImageCopy region = {};
  region.bufferOffset = 0;
  region.bufferRowLength = 0;
  region.bufferImageHeight = 0;

  region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.mipLevel = 0;
  region.imageSubresource.baseArrayLayer = 0;
  region.imageSubresource.layerCount = 1;

  region.imageOffset = {0, 0, 0};
  region.imageExtent = {width, height, 1};
  vkCmdCopyBufferToImage(commandBuffer, buffer, image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
  endSingleTimeCommands(commandBuffer);
}

void VkBackend::createCommandBuffers() {
  _commandBuffers.resize(_swapChainFramebuffers.size());
  VkCommandBufferAllocateInfo allocInfo = {};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = _commandPool;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = (uint32_t)_commandBuffers.size();
  VkResult result =
      vkAllocateCommandBuffers(_device, &allocInfo, _commandBuffers.data());
  vkCheckResult(result, "vkAllocateCommandBuffers");

  std::array<VkClearValue, 5> clearValues = {};
  clearValues[0].color = {0.0f, 0.0f, 0.0f, 0.0f};
  clearValues[1].color = {0.0f, 0.0f, 0.0f, 0.0f};
  clearValues[2].color = {0.0f, 0.0f, 0.0f, 0.0f};
  clearValues[3].color = {0.0f, 0.0f, 0.0f, 0.0f};
  clearValues[4].depthStencil = {1.0f, 0};

  VkCommandBufferBeginInfo beginInfo = {};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
  beginInfo.pInheritanceInfo = nullptr;

  VkRenderPassBeginInfo renderPassInfo = {};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  renderPassInfo.renderPass = _renderPass;
  renderPassInfo.renderArea.offset = {0, 0};
  renderPassInfo.renderArea.extent = _swapChainExtent;

  renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
  renderPassInfo.pClearValues = clearValues.data();

  for (size_t i = 0; i < _commandBuffers.size(); i++) {
    renderPassInfo.framebuffer = _swapChainFramebuffers[i];

    vkBeginCommandBuffer(_commandBuffers[i], &beginInfo);

    vkCmdBeginRenderPass(_commandBuffers[i], &renderPassInfo,
                         VK_SUBPASS_CONTENTS_INLINE);
    // Gpass subpass
    vkCmdBindPipeline(_commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS,
                      _gpassPipeline.pipeline);
    VkDeviceSize offsets[] = {0};
    VkBuffer buffers[] = {_vertexBuffer.buffer};
    vkCmdBindVertexBuffers(_commandBuffers[i], 0, 1, buffers, offsets);
    vkCmdBindIndexBuffer(_commandBuffers[i], _indexBuffer.buffer, 0,
                         VK_INDEX_TYPE_UINT32);
    size_t mesh_id = 0;
    for (auto &mesh : _model.meshes) {
      vkCmdBindDescriptorSets(
          _commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS,
          _gpassPipeline.layout, 0, 1, &_gpassPipeline.descriptorSets[mesh_id],
          0, nullptr);
      vkCmdDrawIndexed(_commandBuffers[i], mesh.indexCount, 1, 0,
                       mesh.vertexOffset, 0);
      mesh_id++;
    }
    // Light subpass
    vkCmdNextSubpass(_commandBuffers[i], VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(_commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS,
                      _lightPipeline.pipeline);
    vkCmdBindDescriptorSets(_commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS,
                            _lightPipeline.layout, 0, 1,
                            &_lightPipeline.descriptorSets[0], 0, nullptr);
    vkCmdDraw(_commandBuffers[i], 3, 1, 0, 0);

    vkCmdEndRenderPass(_commandBuffers[i]);
    result = vkEndCommandBuffer(_commandBuffers[i]);
    vkCheckResult(result, "vkEndCommandBuffer");
  }
}

void VkBackend::createSemaphores() {
  VkResult result;
  VkSemaphoreCreateInfo semaphoreInfo = {};
  semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  result = vkCreateSemaphore(_device, &semaphoreInfo, nullptr,
                             &_imageAvailableSemaphore);
  vkCheckResult(result, "vkCreateSemaphore");
  result = vkCreateSemaphore(_device, &semaphoreInfo, nullptr,
                             &_renderFinishedSemaphore);
  vkCheckResult(result, "vkCreateSemaphore");
}

void VkBackend::cleanupSwapChain() {
  for (size_t i = 0; i < _swapChainFramebuffers.size(); i++) {
    vkDestroyFramebuffer(_device, _swapChainFramebuffers[i], nullptr);
  }

  vkFreeCommandBuffers(_device, _commandPool,
                       static_cast<uint32_t>(_commandBuffers.size()),
                       _commandBuffers.data());

  vkDestroyPipeline(_device, _gpassPipeline.pipeline, nullptr);
  vkDestroyPipelineLayout(_device, _gpassPipeline.layout, nullptr);

  vkDestroyPipeline(_device, _lightPipeline.pipeline, nullptr);
  vkDestroyPipelineLayout(_device, _lightPipeline.layout, nullptr);

  vkDestroyRenderPass(_device, _renderPass, nullptr);

  for (size_t i = 0; i < _swapChainImageViews.size(); i++) {
    vkDestroyImageView(_device, _swapChainImageViews[i], nullptr);
  }

  vkDestroySwapchainKHR(_device, _swapChain, nullptr);
}

void VkBackend::cleanup() {
  cleanupSwapChain();

  vkDestroyImageView(_device, _depth.imageView, nullptr);
  vkDestroyImage(_device, _depth.image, nullptr);
  vkFreeMemory(_device, _depth.imageMemory, nullptr);

  for (auto &texture : _diffuseTextures) {
    vkDestroySampler(_device, texture.sampler, nullptr);
    vkDestroyImageView(_device, texture.imageView, nullptr);
    vkDestroyImage(_device, texture.image, nullptr);
    vkFreeMemory(_device, texture.imageMemory, nullptr);
  }
  for (auto &texture : _specularTextures) {
    vkDestroySampler(_device, texture.sampler, nullptr);
    vkDestroyImageView(_device, texture.imageView, nullptr);
    vkDestroyImage(_device, texture.image, nullptr);
    vkFreeMemory(_device, texture.imageMemory, nullptr);
  }
  for (auto &texture : _normalTextures) {
    vkDestroySampler(_device, texture.sampler, nullptr);
    vkDestroyImageView(_device, texture.imageView, nullptr);
    vkDestroyImage(_device, texture.image, nullptr);
    vkFreeMemory(_device, texture.imageMemory, nullptr);
  }

  vkDestroyDescriptorPool(_device, _gpassPipeline.descriptorPool, nullptr);
  vkDestroyDescriptorSetLayout(_device, _gpassPipeline.descriptorSetLayout,
                               nullptr);

  vkDestroyDescriptorPool(_device, _lightPipeline.descriptorPool, nullptr);
  vkDestroyDescriptorSetLayout(_device, _lightPipeline.descriptorSetLayout,
                               nullptr);

  vkDestroyBuffer(_device, _gpassUniformBuffer.buffer, nullptr);
  vkFreeMemory(_device, _gpassUniformBuffer.bufferMemory, nullptr);

  vkDestroyBuffer(_device, _lightUniformBuffer.buffer, nullptr);
  vkFreeMemory(_device, _lightUniformBuffer.bufferMemory, nullptr);

  vkDestroyBuffer(_device, _vertexBuffer.buffer, nullptr);
  vkFreeMemory(_device, _vertexBuffer.bufferMemory, nullptr);
  vkDestroyBuffer(_device, _indexBuffer.buffer, nullptr);
  vkFreeMemory(_device, _indexBuffer.bufferMemory, nullptr);

  vkDestroySemaphore(_device, _renderFinishedSemaphore, nullptr);
  vkDestroySemaphore(_device, _imageAvailableSemaphore, nullptr);
  vkDestroyCommandPool(_device, _commandPool, nullptr);

  vkDestroyDevice(_device, nullptr);
  DestroyDebugReportCallbackEXT(_instance, _callback, nullptr);
  vkDestroySurfaceKHR(_instance, _surface, nullptr);
  vkDestroyInstance(_instance, nullptr);
  glfwDestroyWindow(_window);
  glfwTerminate();
}

void VkBackend::OnResize() { recreateSwapChain(); }