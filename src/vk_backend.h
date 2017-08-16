#pragma once
#include <chrono>
#include <vector>
#include <map>
#include <set>
#include "Vk_utils.h"
#include "renderer.h"
#include "model.h"

struct VkVertex {
	glm::vec3	pos;
	glm::vec3	normal;
	glm::vec2	texCoord;
	static VkVertexInputBindingDescription getBindingDescription() {
		VkVertexInputBindingDescription bindingDescription = {};
		bindingDescription.binding = 0;
		bindingDescription.stride = sizeof(VkVertex);
		bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		return bindingDescription;
	}

	static std::array<VkVertexInputAttributeDescription, 3> getAttributeDescriptions() {
		std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions = {};
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

		return attributeDescriptions;
	}
};


struct Texture { 
	VkImage				image;
	VkDeviceMemory		imageMemory;
	VkImageView			imageView;
	VkSampler			sampler;
};

struct DepthStencil { 
	VkImage				image;
	VkDeviceMemory		imageMemory;
	VkImageView			imageView;
};

struct Buffer {
	VkBuffer			buffer;
	VkDeviceMemory		bufferMemory;
};


class VkBackend
{
public:
	VkBackend(GLFWwindow *window);
	~VkBackend();

	bool	init(Model model);
	void	drawFrame();
	void	update();
	void	cleanup();
	void	OnResize();

private:

	GLFWwindow	*_window;
	VkInstance	_instance;
	VkDebugReportCallbackEXT _callback;
	VkSurfaceKHR	_surface;
	VkPhysicalDevice _physicalDevice = VK_NULL_HANDLE;
	VkDevice _device;
	VkQueue	_graphicsQueue;
	VkQueue	_presentQueue;
	VkSwapchainKHR _swapChain;
	std::vector<VkImage>	_swapChainImages;
	VkFormat _swapChainImageFormat;
	VkExtent2D	_swapChainExtent;
	std::vector<VkImageView> _swapChainImageViews;
	VkRenderPass	_renderPass;
	VkDescriptorSetLayout	_descriptorSetLayout;
	VkPipelineLayout	_pipelineLayout;
	VkPipeline	_graphicsPipeline;
	std::vector<VkFramebuffer>	_swapChainFramebuffers;
	VkCommandPool	_commandPool;
	std::vector<VkCommandBuffer>	_commandBuffers;
	VkSemaphore	_imageAvailableSemaphore;
	VkSemaphore	_renderFinishedSemaphore;

	std::vector<Buffer> vertexBuffers;
	std::vector<Buffer> indexBuffers;
	
	VkBuffer	_uniformBuffer;
	VkDeviceMemory	_uniformBufferMemory;

	VkDescriptorPool	_descriptorPool;
	VkDescriptorSet	_descriptorSet;

	Texture		_texture;
	DepthStencil	_depth;

	Model _model;

	void	createInstance();
	void	setupDebugCallback();
	void	createSurface();
	void	pickPhysicalDevice();
	void	createLogicalDevice();
	void	createSwapChain();
	void	createImageViews();
	void	createRenderPass();
	void	createDescriptorSetLayout();
	void	createGraphicsPipeline();
	void	createFramebuffers();
	void	createCommandPool();
	void	createDepthResources();
	Texture	createTextureImage(const std::string filepath);
	void	createTextureImageView(Texture &texture);
	void	createTextureSampler(Texture &texture);
	VkImageView	createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags);
	void	createImage(uint32_t width, uint32_t height, VkFormat format,
				VkImageTiling tiling, VkImageUsageFlags usage,
				VkMemoryPropertyFlags properties, VkImage& image,
				VkDeviceMemory& imageMemory);
	Buffer	createVertexBuffer(std::vector<Vertex> vertices);
	Buffer	createIndexBuffer(std::vector<uint32_t> indices);
	void	createUniformBuffer();
	void	createDescriptorPool();
	void	createDescriptorSet();
	void	createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
				VkMemoryPropertyFlags properties, VkBuffer &buffer,
				VkDeviceMemory &bufferMemory);
	void	copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
				VkCommandBuffer beginSingleTimeCommands();
	void	endSingleTimeCommands(VkCommandBuffer commandBuffer);
	void	transitionImageLayout(VkImage image, VkFormat format,
				VkImageLayout oldLayout, VkImageLayout newLayout);
	void	copyBufferToImage(VkBuffer buffer, VkImage image, 
				uint32_t width, uint32_t height);
	void	createCommandBuffers();
	void	createSemaphores();
	void	recreateSwapChain();
	void	cleanupSwapChain();

};