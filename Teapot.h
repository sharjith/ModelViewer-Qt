#pragma once

#include "GridMesh.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
using glm::vec3;
using glm::mat4;
using glm::mat3;
using glm::vec4;

class Teapot : public GridMesh
{
public:
	Teapot(QOpenGLShaderProgram* prog, float size, int grid, const glm::mat4& lidTransform);

	virtual TriangleMesh* clone();

private:
	//unsigned int faces;
	int _size;
	mat4 _lidTransform;

	void generatePatches(std::vector<float>& p,
		std::vector<float>& n,
		std::vector<float>& tc, std::vector<float>& tg, std::vector<float>& bt,
		std::vector<unsigned int>& el, int grid);
	void buildPatchReflect(int patchNum,
		std::vector<float>& B, std::vector<float>& dB,
		std::vector<float>& v, std::vector<float>& n,
		std::vector<float>& tc, std::vector<float>& tg, std::vector<float>& bt, std::vector<unsigned int>& el,
		int& index, int& elIndex, int& tcIndex, int grid,
		bool reflectX, bool reflectY);
	void buildPatch(glm::vec3 patch[][4],
		std::vector<float>& B, std::vector<float>& dB,
		std::vector<float>& v, std::vector<float>& n,
		std::vector<float>& tc, std::vector<float>& tg, std::vector<float>& bt, std::vector<unsigned int>& el,
		int& index, int& elIndex, int& tcIndex, int grid, glm::mat3 reflect,
		bool invertNormal);
	void getPatch(int patchNum, glm::vec3 patch[][4], bool reverseV);

	void computeBasisFunctions(std::vector<float>& B, std::vector<float>& dB, int grid);
	glm::vec3 evaluate(int gridU, int gridV, std::vector<float>& B, glm::vec3 patch[][4]);
	glm::vec3 evaluateNormal(int gridU, int gridV, std::vector<float>& B, std::vector<float>& dB, glm::vec3 patch[][4], glm::vec3& tgt, glm::vec3& btg);
	void moveLid(int grid, std::vector<float>& p, const glm::mat4& lidTransform);
};
