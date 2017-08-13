#pragma once
#include <map>
#include <set>
#include "Vk_utils.h"

class VkBackend
{
public:
	VkBackend(GLFWwindow *window);
	~VkBackend();

	bool	init();
	void	cleanup();

private:

	GLFWwindow	*_window;
	VkInstance	_instance;
	VkDebugReportCallbackEXT _callback;
	VkSurfaceKHR	_surface;
	VkPhysicalDevice _physicalDevice = VK_NULL_HANDLE;
	VkDevice _device;
	VkQueue	_graphicsQueue;
	VkQueue	_presentQueue;

	void	createInstance();
	void	setupDebugCallback();
	void	createSurface();
	void	pickPhysicalDevice();
	void	createLogicalDevice();
};


