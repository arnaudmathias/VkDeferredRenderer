#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>
#include <iostream>
#include <stdexcept>
#include <functional>
#include "model.h"
#include "vk_backend.h"

static void onWindowResized(GLFWwindow* window, int width, int height) {
	if (width == 0 || height == 0) return;

	VkBackend* backend = reinterpret_cast<VkBackend*>(glfwGetWindowUserPointer(window));
	backend->OnResize();
}

int main() {
	glfwInit();

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	GLFWwindow* window = glfwCreateWindow(1280, 720, "Vulkan Deferred renderer", nullptr, nullptr);

	Model model;
	model.load("models/sponza/sponza.obj");
	//model.load("models/sibenik/sibenik.obj");
	VkBackend vulkanBackend(window);
	vulkanBackend.init(model);
	glfwSetWindowUserPointer(window, &vulkanBackend);
	glfwSetWindowSizeCallback(window, onWindowResized);
	while (!glfwWindowShouldClose(window)) {
		glfwPollEvents();
		vulkanBackend.update();
		vulkanBackend.drawFrame();
	}

	glfwDestroyWindow(window);

	glfwTerminate();
	vulkanBackend.cleanup();
	return 0;
}
