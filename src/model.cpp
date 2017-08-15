#include "model.h"
#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

Mesh::Mesh() {
	
}

Mesh::Mesh(std::string ambient_tex, std::string diffuse_tex, std::string specular_tex) :
	ambient_texname(ambient_tex),
	diffuse_texname(diffuse_tex),
	specular_texname(specular_tex) {
	
}

Mesh::~Mesh() {

}

Model::Model() {

}

Model::~Model() {

}

static std::string GetBaseDir(const std::string &filepath) {
	if (filepath.find_last_of("/\\") != std::string::npos)
		return filepath.substr(0, filepath.find_last_of("/\\"));
	return "";
}


void Model::load(const std::string filename) {
	tinyobj::attrib_t attrib;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;
	std::string err;
	std::string basedir = GetBaseDir(filename);
	if (basedir.empty())
		basedir = ".";
	basedir += "/";

	if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &err, filename.c_str(), basedir.c_str())) {
		throw std::runtime_error(err);
	}
	if (materials.empty()) {
		materials.push_back(tinyobj::material_t());
	}
	for (const auto &material : materials) {
		meshes.push_back(Mesh(material.ambient_texname, material.diffuse_texname, material.specular_texname));
	}
	for (const auto& shape : shapes) {
		size_t face_id = 0;
		for (const auto& index : shape.mesh.indices) {
			
			Vertex vertex = {};
			vertex.pos = {
				attrib.vertices[3 * index.vertex_index + 0],
				attrib.vertices[3 * index.vertex_index + 1],
				attrib.vertices[3 * index.vertex_index + 2]
			};
			vertex.texCoord = {
				attrib.texcoords[2 * index.texcoord_index + 0],
				1.0f - attrib.texcoords[2 * index.texcoord_index + 1]
			};
			vertex.color = { 1.0f, 1.0f, 1.0f };
			int material_id;
			//TODO: figure out why I need this
			if (face_id < shape.mesh.material_ids.size())
				material_id = shape.mesh.material_ids[face_id];
			else
				material_id = 0;
			if (material_id < 0 || material_id >= static_cast<int>(materials.size())) {
				material_id = 0;
			}
			this->meshes[material_id].vertices.push_back(vertex);
			this->meshes[material_id].indices.push_back(static_cast<int>(this->meshes[material_id].indices.size()));
			face_id++;
		}
	}
	
}