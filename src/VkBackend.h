#pragma once
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <chrono>
#include <vector>
#include <map>
#include <set>
#include "Vk_utils.h"

class VkBackend
{
public:
	VkBackend(GLFWwindow *window);
	~VkBackend();

	bool	init();
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
	VkBuffer	_vertexBuffer;
	VkDeviceMemory	_vertexBufferMemory;
	VkBuffer	_indexBuffer;
	VkDeviceMemory	_indexBufferMemory;
	VkBuffer	_uniformBuffer;
	VkDeviceMemory	_uniformBufferMemory;
	VkDescriptorPool	_descriptorPool;
	VkDescriptorSet	_descriptorSet;

	VkImage	_textureImage;
	VkDeviceMemory	_textureImageMemory;
	VkImageView	_textureImageView;
	VkSampler	_textureSampler;

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
	void	createTextureImage();
	void	createTextureImageView();
	void	createTextureSampler();

	VkImageView	createImageView(VkImage image, VkFormat format);
	void	createImage(uint32_t width, uint32_t height, VkFormat format,
				VkImageTiling tiling, VkImageUsageFlags usage,
				VkMemoryPropertyFlags properties, VkImage& image,
				VkDeviceMemory& imageMemory);
	void	createVertexBuffer();
	void	createIndexBuffer();
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


