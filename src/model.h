#pragma once
#include <string>
#include <vector>
#include <iostream>
#include "renderer.h"

struct Vertex {
	glm::vec3	pos;
	glm::vec3	color;
	glm::vec2	texCoord;
};

class Mesh
{
public:
	Mesh();
	Mesh(std::string ambient_tex, std::string diffuse_tex, std::string specular_tex);
	~Mesh();
	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
	std::string ambient_texname;
	std::string diffuse_texname;
	std::string specular_texname;

private:
		
};


class Model {
public:
	Model();
	~Model();

	void	load(const std::string filepath);
	std::vector<Mesh> meshes;
private:

};