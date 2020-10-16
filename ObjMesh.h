#pragma once

#include "TriangleMesh.h"
#include "AABB.h"

#include <vector>
#include <glm/glm.hpp>
#include <string>
#include <memory>

class ObjMesh : public TriangleMesh
{
private:
	bool drawAdj;

public:
	static std::unique_ptr<ObjMesh> load(QOpenGLShaderProgram* prog, const char* fileName, bool center = false, bool genTangents = false);
	static std::unique_ptr<ObjMesh> loadWithAdjacency(QOpenGLShaderProgram* prog, const char* fileName, bool center = false);

	virtual TriangleMesh* clone();

	void render() override;

protected:
	ObjMesh(QOpenGLShaderProgram* prog);

	Aabb bbox;

	// Helper classes used for loading
	class GlMeshData
	{
	public:
		std::vector<float> points;
		std::vector<float> normals;
		std::vector<float> texCoords;
		std::vector<unsigned int> faces;
		std::vector<float> tangents;

		void clear()
		{
			points.clear();
			normals.clear();
			texCoords.clear();
			faces.clear();
			tangents.clear();
		}
		void center(Aabb& bbox);
		void convertFacesToAdjancencyFormat();
	};

	class ObjMeshData
	{
	public:
		class ObjVertex
		{
		public:
			int pIdx;
			int nIdx;
			int tcIdx;

			ObjVertex()
			{
				pIdx = -1;
				nIdx = -1;
				tcIdx = -1;
			}

			ObjVertex(std::string& vertString);
			std::string str()
			{
				return std::to_string(pIdx) + "/" + std::to_string(tcIdx) + "/" + std::to_string(nIdx);
			}
		};

		std::vector<glm::vec3> points;
		std::vector<glm::vec3> normals;
		std::vector<glm::vec2> texCoords;
		std::vector<ObjVertex> faces;
		std::vector<glm::vec4> tangents;

		ObjMeshData() {}

		void generateNormalsIfNeeded();
		void generateTangents();
		void load(const char* fileName, Aabb& bbox);
		void toGlMesh(GlMeshData& data);
	};
};
