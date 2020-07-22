#include "Cylinder.h"

#include <cstdio>
#include <cmath>

#include <glm/gtc/constants.hpp>
#include <glm/vec3.hpp>
#include <glm/glm.hpp>

Cylinder::Cylinder(QOpenGLShaderProgram *prog, float radius, float height, GLuint nSlices, GLuint nStacks) : QuadMesh(prog, "Cylinder")
{
	int nVerts = ((nSlices + 1) * (nStacks + 1)) + (nSlices * 2) + 4;
	int elements = ((nSlices * 2 * (nStacks)) * 3) + (nSlices * 2) * 3;

	// Verts
	std::vector<GLfloat> p(3 * nVerts);
	// Normals
	std::vector<GLfloat> n(3 * nVerts);
	// Tex coords
	std::vector<GLfloat> tex(2 * nVerts);
	// Elements
	std::vector<GLuint> el(elements);

	// Generate positions and normals
	GLfloat theta, phi;
	GLfloat thetaFac = glm::two_pi<float>() / nSlices;
	GLfloat phiFac = 1.0f / nStacks;
	GLfloat nx, ny, nz, s, t;
	GLuint idx = 0, tIdx = 0;
	for (GLuint i = 0; i <= nSlices; i++)
	{
		theta = i * thetaFac;
		s = (GLfloat)i / nSlices;
		for (GLuint j = 0; j <= nStacks; j++)
		{
			phi = j * phiFac;
			t = (GLfloat)j / nStacks;
			nx = cosf(theta);
			ny = sinf(theta);
			nz = (phi);
			p[idx] = radius * nx;
			p[idx + 1] = radius * ny;
			p[idx + 2] = height * nz - height / 2.0f;
			glm::vec3 o(0, 0, (nz * height));
			glm::vec3 v((nx * radius), (ny * radius), (nz * height) - height / 2.0f);
			glm::vec3 normal = v - o;
			normal = glm::normalize(normal);
			normal = -normal;
			n[idx] = normal.x;
			n[idx + 1] = normal.y;
			n[idx + 2] = normal.z;
			idx += 3;

			tex[tIdx] = s;
			tex[tIdx + 1] = t;
			tIdx += 2;
		}
	}

	// bottom face
	for (GLuint i = 0; i <= nSlices; i++)
	{
		theta = i * thetaFac;
		s = (GLfloat)i / nSlices;
		nx = cosf(theta);
		ny = sinf(theta);
		nz = 0;

		p[idx] = radius * nx;
		p[idx + 1] = radius * ny;
		p[idx + 2] = nz - height / 2.0f;
		n[idx] = 0;
		n[idx + 1] = 0;
		n[idx + 2] = -1.0f;
		idx += 3;
		s = (-nx + 1.0f) * 0.5f;
		t = (ny + 1.0f) * 0.5f;
		tex[tIdx] = s;
		tex[tIdx + 1] = t;
		tIdx += 2;
	}

	// bottom center
	p[idx] = 0;
	p[idx + 1] = 0;
	p[idx + 2] = -height / 2.0f;
	n[idx] = 0;
	n[idx + 1] = 0;
	n[idx + 2] = -1.0f;
	idx += 3;
	tex[tIdx] = 0.5;
	tex[tIdx + 1] = 0.5;
	tIdx += 2;

	// top face
	for (GLuint i = 0; i <= nSlices; i++)
	{
		theta = i * thetaFac;
		s = (GLfloat)i / nSlices;
		nx = cosf(theta);
		ny = sinf(theta);
		nz = height;

		p[idx] = radius * nx;
		p[idx + 1] = radius * ny;
		p[idx + 2] = nz - height / 2.0f;
		n[idx] = 0;
		n[idx + 1] = 0;
		n[idx + 2] = 1.0f;
		idx += 3;
		s = (nx + 1.0f) * 0.5f;
		t = (ny + 1.0f) * 0.5f;
		tex[tIdx] = s;
		tex[tIdx + 1] = t;
		tIdx += 2;
	}

	// top center
	p[idx] = 0;
	p[idx + 1] = 0;
	p[idx + 2] = height / 2;
	n[idx] = 0;
	n[idx + 1] = 0;
	n[idx + 2] = 1.0f;
	idx += 3;
	tex[tIdx] = 0.5;
	tex[tIdx + 1] = 0.5;
	tIdx += 2;

	// Generate the element list
	// Body
	idx = 0;
	/*for (GLuint i = 0; i < nSlices; i++)
	{
		GLuint stackStart = i * (nStacks + 1);
		GLuint nextStackStart = (i + 1) * (nStacks + 1);
		for (GLuint j = 0; j < nStacks; j++)
		{
			el[idx + 0] = stackStart + j;
			el[idx + 1] = stackStart + j + 1;
			el[idx + 2] = nextStackStart + j + 1;
			el[idx + 3] = nextStackStart + j;
			el[idx + 4] = stackStart + j;
			el[idx + 5] = nextStackStart + j + 1;
			idx += 6;
		}
	}*/
	for (GLuint i = 0; i < nSlices; i++)
	{
		GLuint stackStart = i * (nStacks + 1);
		GLuint nextStackStart = (i + 1) * (nStacks + 1);
		for (GLuint j = 0; j < nStacks; j++)
		{
			el[idx + 0] = stackStart + j;
			el[idx + 1] = stackStart + j + 1;
			el[idx + 2] = nextStackStart + j + 1;
			el[idx + 3] = nextStackStart + j;
			//el[idx + 4] = stackStart + j;
			//el[idx + 5] = nextStackStart + j + 1;
			idx += 4;
		}
	}

	// Bottom face
	GLuint j = ((nSlices + 1) * (nStacks + 1));
	for (GLuint i = 0; i < nSlices; i++, j++)
	{
		el[idx + 0] = j;
		el[idx + 1] = ((nSlices + 1) * (nStacks + 1)) + nSlices + 1;
		el[idx + 2] = j + 1;
		el[idx + 3] = j;
		idx += 4;
	}

	// Top face
	j = ((nSlices + 1) * (nStacks + 1)) + (nSlices + 2);
	for (GLuint i = 0; i < nSlices; i++, j++)
	{
		el[idx + 0] = j;
		el[idx + 1] = j + 1;
		el[idx + 2] = (((nSlices + 1) * (nStacks + 1)) + nSlices * 2) + 3;
		el[idx + 3] = j;
		idx += 4;
	}

	initBuffers(&el, &p, &n, &tex);

	_boundingSphere.setCenter(0, 0, 0);
	_boundingSphere.setRadius(sqrt(radius * radius + height / 2 * height / 2));
}
