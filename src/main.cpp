#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <functional>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
#include <iostream>
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include "graphics_backend.h"
#include "model.h"
#include "vk_backend.h"

void updateFpsCounter(GLFWwindow *window) {
  static double previous_seconds = glfwGetTime();
  static int frame_count;
  double current_seconds = glfwGetTime();
  double elapsed_seconds = current_seconds - previous_seconds;
  if (elapsed_seconds > 0.25) {
    previous_seconds = current_seconds;
    double fps = (double)frame_count / elapsed_seconds;
    std::ostringstream title;
    title << "Vulkan Deferred @ " << std::fixed << std::setprecision(1) << fps << " fps";
    glfwSetWindowTitle(window, title.str().c_str());
    frame_count = 0;
  }
  frame_count++;
}


static void onWindowResized(GLFWwindow* window, int width, int height) {
  if (width == 0 || height == 0) return;

  GraphicsBackend* backend =
      reinterpret_cast<GraphicsBackend*>(glfwGetWindowUserPointer(window));
  backend->onResize();
}

int main() {
  glfwInit();

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  GLFWwindow* window =
      glfwCreateWindow(1280, 720, "Vulkan Deferred renderer", nullptr, nullptr);

  Model model;
  model.load("models/sponza/sponza.obj");

  VkBackend vulkanBackend = VkBackend();
  vulkanBackend.init(window, model);
  glfwSetWindowUserPointer(window, &vulkanBackend);
  glfwSetWindowSizeCallback(window, onWindowResized);
  while (!glfwWindowShouldClose(window)) {
    updateFpsCounter(window);
    glfwPollEvents();
    vulkanBackend.update();
    vulkanBackend.drawFrame();
  }

  glfwDestroyWindow(window);

  glfwTerminate();
  vulkanBackend.cleanup();
  return 0;
}
