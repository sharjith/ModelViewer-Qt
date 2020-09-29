#pragma once

#include "GridMesh.h"
#include <glm/glm.hpp>

class Teapot : public GridMesh
{
public:
	Teapot(QOpenGLShaderProgram* prog, float size, int grid, const glm::mat4& lidTransform);

private:
	//unsigned int faces;
	int _size;

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
