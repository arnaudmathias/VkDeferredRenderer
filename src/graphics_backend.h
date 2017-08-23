#pragma once
#include <GLFW/glfw3.h>
#include <fstream>
#include <vector>
#include "model.h"

class GraphicsBackend {
 public:
  GraphicsBackend(){};
  virtual void init(GLFWwindow* window, Model model) = 0;
  virtual void update() = 0;
  virtual void drawFrame() = 0;
  virtual void cleanup() = 0;
  virtual void onResize() = 0;

 protected:
  GLFWwindow* _window;
  Model _model;
};

static std::vector<char> readShader(const std::string& filename) {
  std::ifstream file(filename, std::ios::ate | std::ios::binary);

  if (!file.is_open()) {
    throw std::runtime_error("failed to open file!");
  }
  size_t fileSize = (size_t)file.tellg();
  std::vector<char> buffer(fileSize);
  file.seekg(0);
  file.read(buffer.data(), fileSize);
  file.close();

  return buffer;
}