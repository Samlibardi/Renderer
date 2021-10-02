#pragma once

#include <vector>
#include <map>

#include "Vertex.h"

class Mesh
{
public:
	bool isIndexed = true;
	std::vector<Vertex> vertices;
	std::map<size_t, size_t> verticesByMaterial;
	std::vector<uint32_t> triangleIndices;

	void calculateTangents();
};

