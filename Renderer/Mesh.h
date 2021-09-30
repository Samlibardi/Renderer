#pragma once

#include <vector>
#include <map>

#include "Vertex.h"
#include "Material.h"

class Mesh
{
public:
	std::vector<Vertex> vertices;
	std::vector<Material> materials;
	std::map<size_t, size_t> verticesByMaterial;
	std::vector<uint16_t> triangleIndices;

};

