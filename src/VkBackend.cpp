#include "VkBackend.h"

VkBackend::VkBackend(GLFWwindow *window) : _window(window) {

}


VkBackend::~VkBackend() {

}


bool	VkBackend::init() {
	createInstance();
	setupDebugCallback();
	createSurface();
	pickPhysicalDevice();
	createLogicalDevice();

	return true;
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
		createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
		createInfo.ppEnabledLayerNames = validationLayers.data();
	} else {
		createInfo.enabledLayerCount = 0;
	}

	VkResult result;
	result = vkCreateInstance(&createInfo, nullptr, &_instance);
	vkCheckResult(result, "vkCreateInstance");
}

void	VkBackend::setupDebugCallback() {
	if (!enableValidationLayers)
		return;
	VkDebugReportCallbackCreateInfoEXT createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
	createInfo.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
	createInfo.pfnCallback = debugCallback;
	if (CreateDebugReportCallbackEXT(_instance, &createInfo, nullptr, &_callback) != VK_SUCCESS) {
		throw std::runtime_error("failed to set up debug callback!");
	}
}

void	VkBackend::createSurface() {
	if (glfwCreateWindowSurface(_instance, _window, nullptr, &_surface) != VK_SUCCESS) {
		throw std::runtime_error("failed to create window surface!");
	}
}

void	VkBackend::pickPhysicalDevice() {
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
	for (const auto& device : devices) {
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
	
void	VkBackend::createLogicalDevice() {
	QueueFamilyIndices indices = findQueueFamilies(_physicalDevice, _surface);
	std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
	std::set<int> uniqueQueueFamilies = {indices.graphicsFamily, indices.presentFamily};

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

	VkDeviceCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
	createInfo.pQueueCreateInfos = queueCreateInfos.data();

	createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
	createInfo.ppEnabledExtensionNames = deviceExtensions.data();

	createInfo.pEnabledFeatures = &deviceFeatures;
	createInfo.enabledExtensionCount = 0;
	if (enableValidationLayers) {
		createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
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

void	VkBackend::cleanup() {
	vkDestroyDevice(_device, nullptr);
	DestroyDebugReportCallbackEXT(_instance, _callback, nullptr);
	vkDestroySurfaceKHR(_instance, _surface, nullptr);
	vkDestroyInstance(_instance, nullptr);
	glfwDestroyWindow(_window);
	glfwTerminate();
}