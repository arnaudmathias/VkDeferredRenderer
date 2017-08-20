#pragma once
#include <iostream>
#include <string>
#include <vector>
#include "renderer.h"

struct Vertex {
  glm::vec3 pos;
  glm::vec3 normal;
  glm::vec2 texCoord;
  glm::vec3 tangent;
};

class Mesh {
 public:
  Mesh();
  Mesh(uint32_t count, int32_t offset, std::string ambient_tex,
       std::string diffuse_tex, std::string specular_tex,
       std::string normal_tex);
  ~Mesh();
  uint32_t indexCount;   // vertices count
  int32_t vertexOffset;  // offset in vertex array
  std::string ambient_texname;
  std::string diffuse_texname;
  std::string specular_texname;
  std::string normal_texname;

 private:
};

class Model {
 public:
  Model();
  ~Model();

  void load(const std::string filepath);
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
  std::vector<Mesh> meshes;

 private:
};