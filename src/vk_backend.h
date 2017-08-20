#pragma once
#include <chrono>
#include <map>
#include <set>
#include <vector>
#include "Vk_utils.h"
#include "model.h"
#include "renderer.h"

struct VkVertex {
  glm::vec3 pos;
  glm::vec3 normal;
  glm::vec2 texCoord;
  glm::vec3 tangent;
  static VkVertexInputBindingDescription getBindingDescription() {
    VkVertexInputBindingDescription bindingDescription = {};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(VkVertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return bindingDescription;
  }

  static std::array<VkVertexInputAttributeDescription, 4>
  getAttributeDescriptions() {
    std::array<VkVertexInputAttributeDescription, 4> attributeDescriptions = {};
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(VkVertex, pos);

    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(VkVertex, normal);

    attributeDescriptions[2].binding = 0;
    attributeDescriptions[2].location = 2;
    attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[2].offset = offsetof(VkVertex, texCoord);

    attributeDescriptions[3].binding = 0;
    attributeDescriptions[3].location = 3;
    attributeDescriptions[3].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[3].offset = offsetof(VkVertex, tangent);

    return attributeDescriptions;
  }
};

struct gPassUbo {
  glm::mat4 model;
  glm::mat4 view;
  glm::mat4 proj;
};

struct Light {
  glm::vec4 position;
  glm::vec3 color;
  float radius;
};

struct lightUbo {
  glm::vec4 viewPosition;
  std::array<Light, 6> lights;
};

struct Texture {
  VkImage image;
  VkDeviceMemory imageMemory;
  VkImageView imageView;
  VkSampler sampler;
};

struct DepthStencil {
  VkImage image;
  VkDeviceMemory imageMemory;
  VkImageView imageView;
};

struct Buffer {
  VkBuffer buffer;
  VkDeviceMemory bufferMemory;
};

struct Attachment {
  VkImage image;
  VkDeviceMemory memory;
  VkImageView imageView;
  VkFormat format;
};

struct Pipeline {
  VkPipelineLayout layout;
  VkPipeline pipeline;
  VkDescriptorSetLayout descriptorSetLayout;
  VkDescriptorPool descriptorPool;
  std::vector<VkDescriptorSet> descriptorSets;
};

class VkBackend {
 public:
  VkBackend(GLFWwindow *window);
  ~VkBackend();

  bool init(Model model);
  void drawFrame();
  void update();
  void cleanup();
  void OnResize();

 private:
  GLFWwindow *_window;
  VkInstance _instance;
  VkDebugReportCallbackEXT _callback;
  VkSurfaceKHR _surface;
  VkPhysicalDevice _physicalDevice = VK_NULL_HANDLE;
  VkDevice _device;
  VkQueue _graphicsQueue;
  VkQueue _presentQueue;
  VkSwapchainKHR _swapChain;
  std::vector<VkImage> _swapChainImages;
  VkFormat _swapChainImageFormat;
  VkExtent2D _swapChainExtent;
  std::vector<VkImageView> _swapChainImageViews;

  VkRenderPass _renderPass;

  Pipeline _gpassPipeline;  // Geometry-pass (1st subpass)
  Pipeline _lightPipeline;

  std::vector<VkFramebuffer> _swapChainFramebuffers;
  VkCommandPool _commandPool;
  std::vector<VkCommandBuffer> _commandBuffers;
  VkSemaphore _imageAvailableSemaphore;
  VkSemaphore _renderFinishedSemaphore;

  Buffer _vertexBuffer;
  Buffer _indexBuffer;

  Buffer _gpassUniformBuffer;
  Buffer _lightUniformBuffer;

  // VkDescriptorPool	_descriptorPool;

  // std::vector<Texture> _ambientTextures;
  std::vector<Texture> _diffuseTextures;
  std::vector<Texture> _specularTextures;
  std::vector<Texture> _normalTextures;
  DepthStencil _depth;
  std::vector<Attachment> _gBufferAttachments;

  Model _model;

  void createInstance();
  void setupDebugCallback();
  void createSurface();
  void pickPhysicalDevice();
  void createLogicalDevice();
  void createSwapChain();
  void createImageViews();
  void createRenderPass();
  VkDescriptorSetLayout createGPassDescriptorSetLayout();
  VkDescriptorSetLayout createLightDescriptorSetLayout();
  Pipeline createGraphicsPipeline(const std::string vertexShader,
                                  const std::string fragShader,
                                  VkDescriptorSetLayout setLayout,
                                  uint32_t subpass_id,
                                  uint32_t colorAttachementCount);
  void createFramebuffers();
  void createCommandPool();
  void createDepthResources();
  void createGBufferAttachments();
  Texture createTextureImage(const std::string filepath);
  void createTextureImageView(Texture &texture);
  void createTextureSampler(Texture &texture);
  VkImageView createImageView(VkImage image, VkFormat format,
                              VkImageAspectFlags aspectFlags);
  void createImage(uint32_t width, uint32_t height, VkFormat format,
                   VkImageTiling tiling, VkImageUsageFlags usage,
                   VkMemoryPropertyFlags properties, VkImage &image,
                   VkDeviceMemory &imageMemory);

  Buffer createVertexBuffer(std::vector<Vertex> vertices);
  Buffer createIndexBuffer(std::vector<uint32_t> indices);
  Buffer createUniformBuffer(size_t bufferSize);

  VkDescriptorPool createGPassDescriptorPool(uint32_t poolSize);
  VkDescriptorSet createGPassDescriptorSet(VkDescriptorPool pool,
                                           VkDescriptorSetLayout layout,
                                           const Texture &diffuse,
                                           const Texture &specular,
                                           const Texture &normal);

  VkDescriptorPool createLightDescriptorPool(uint32_t poolSize);
  VkDescriptorSet createLightDescriptorSet(VkDescriptorPool pool,
                                           VkDescriptorSetLayout layout);

  void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                    VkMemoryPropertyFlags properties, VkBuffer &buffer,
                    VkDeviceMemory &bufferMemory);
  void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
  VkCommandBuffer beginSingleTimeCommands();
  void endSingleTimeCommands(VkCommandBuffer commandBuffer);
  void transitionImageLayout(VkImage image, VkFormat format,
                             VkImageLayout oldLayout, VkImageLayout newLayout);
  void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width,
                         uint32_t height);
  void createCommandBuffers();
  void createSemaphores();
  void recreateSwapChain();
  void cleanupSwapChain();
};