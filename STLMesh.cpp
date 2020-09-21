#include "STLMesh.h"
#include "stl_reader.h"

#include <glm/glm.hpp>
#include <iostream>
#include <QProgressDialog>

/*
STLMesh::STLMesh(QOpenGLShaderProgram *prog, QString stlfilepath) : TriangleMesh(prog, "STLMesh"),
	_stlFilePath(stlfilepath)
{
	// First we'll read the stl data into local arrays. Each triplet in `rawPoints` corresponds to one
	// 3D point and each triplet in `rawTriNormals` to one 3D triangle normal.
	std::vector <float> rawPoints, rawTriNormals;
	std::vector <unsigned int> triIndices, solids;

	stl_reader::ReadStlFile (_stlFilePath.toLocal8Bit().data(), rawPoints, rawTriNormals, triIndices, solids);

	const size_t numTris = triIndices.size() / 3;

	// We'll use vector types from the glm library for more readable math.
	std::vector <glm::vec3> points;
	for (size_t i = 0; i + 2 < rawPoints.size (); i += 3)
		points.push_back ({rawPoints [i], rawPoints [i+1], rawPoints [i+2]});

	std::vector <glm::vec3> triNormals;
	for (size_t i = 0; i + 2 < rawTriNormals.size (); i += 3)
		triNormals.push_back ({rawTriNormals [i], rawTriNormals [i+1], rawTriNormals [i+2]});

	// In `smoothVertexNormals` we'll store the normal of each vertex. They are computed
	// by first summing up the normals of adjacent triangles and then normalizing the
	// resulting normal to get a unit normal for each vertex.
	// Those `smoothVertexNormals` can then be used alongside `points` and `triIndices` to draw indexed
	// primitives using `glDrawElements` or similar methods.
	// While smooth vertex normals lead to a smooth and round appearance of objects, they are are not
	// well suited to represent the surface orientation at crease vertices, i.e., vertices at which
	// triangles meet at large angles. See `flatPoints` and `flatNormals` below for an alternate approach.
	std::vector <glm::vec3> smoothVertexNormals (points.size (), glm::vec3 (0));
	for (size_t itri = 0; itri < numTris; ++itri)
	{
		const size_t baseIndex = itri * 3;
		for (size_t icorner = 0; icorner < 3; ++icorner)
			smoothVertexNormals [triIndices [baseIndex + icorner]] += triNormals [itri];
	}

	for (auto& n : triNormals)
		n = glm::normalize (n);

	// Sometimes flat shading or partial flat shading is preferrable to smooth shading. While OpenGL
	// offers a shading mode for that, the results from that mode are often not accurate, since only
	// the normal of the first vertex of each triangle is considered for lighting calculations. Here is
	// a simple example on how to set up points and normals for accurate flat shading. Note that accurate
	// flat shading can also be achieved using geometry shaders. But this is probably out of the scope
	// of this example...
	// To render the resulting `flatPoints` and `flatNormals`, the `triIndices` array can now be ignored
	// and a method like `glDrawArrays` should be used.

	std::vector <glm::vec3> flatPoints, flatNormals;
	for (size_t itri = 0; itri < numTris; ++itri)
	{
		const size_t baseIndex = itri * 3;
		for (size_t icorner = 0; icorner < 3; ++icorner)
		{
			flatPoints.push_back (points [triIndices [baseIndex + icorner]]);
			flatNormals.push_back (triNormals [itri]);
		}
	}

	std::vector<float> coords, norms;
	for(glm::vec3 a : points)
	{
		coords.push_back(a[0]);
		coords.push_back(a[1]);
		coords.push_back(a[2]);
	}

	for(glm::vec3 a : smoothVertexNormals)
	{
		norms.push_back(a[0]);
		norms.push_back(a[1]);
		norms.push_back(a[2]);
	}

	initBuffers(&triIndices, &coords, &norms);
	computeBoundingSphere(rawPoints);
}
 */

STLMesh::STLMesh(QOpenGLShaderProgram* prog, QString stlfilepath) : TriangleMesh(prog, "STLMesh"),
_stlFilePath(stlfilepath),
_loaded(false)
{
	QFileInfo fi(stlfilepath);
	QString fileName = fi.baseName();
	setName(fileName);

	std::vector<unsigned int> elements;
	std::vector<float> points;
	std::vector<float> norms;
	std::vector<float> tangents;
	std::vector<float> bitangents;
	std::vector<float> texcords;

	std::vector<float> normals;
	std::vector<unsigned int> tris, solids;

	std::vector<float> xCoords;
	std::vector<float> yCoords;
	std::vector<float> zCoords;

	try
	{
		bool success = stl_reader::ReadStlFile(_stlFilePath.toLocal8Bit().data(), points, normals, tris, solids);

		if (success && points.size())
		{
			for (size_t var = 0; var < points.size(); var += 9)
			{
				QVector3D o(points[var + 0], points[var + 1], points[var + 2]);
				QVector3D p(points[var + 3], points[var + 4], points[var + 5]);
				QVector3D q(points[var + 6], points[var + 7], points[var + 8]);

				QVector3D op = p - o;
				QVector3D oq = q - o;

				QVector3D n = QVector3D::crossProduct(op, oq);

				norms.push_back(n.x());
				norms.push_back(n.y());
				norms.push_back(n.z());

				norms.push_back(n.x());
				norms.push_back(n.y());
				norms.push_back(n.z());

				norms.push_back(n.x());
				norms.push_back(n.y());
				norms.push_back(n.z());

				tangents.push_back(op.x());
				tangents.push_back(op.y());
				tangents.push_back(op.z());

				tangents.push_back(op.x());
				tangents.push_back(op.y());
				tangents.push_back(op.z());

				tangents.push_back(op.x());
				tangents.push_back(op.y());
				tangents.push_back(op.z());

				QVector3D bi = QVector3D::crossProduct(n, op);
				bitangents.push_back(bi.x());
				bitangents.push_back(bi.y());
				bitangents.push_back(bi.z());

				bitangents.push_back(bi.x());
				bitangents.push_back(bi.y());
				bitangents.push_back(bi.z());

				bitangents.push_back(bi.x());
				bitangents.push_back(bi.y());
				bitangents.push_back(bi.z());

				xCoords.push_back(points[var + 0]);
				xCoords.push_back(points[var + 3]);
				xCoords.push_back(points[var + 6]);

				yCoords.push_back(points[var + 1]);
				yCoords.push_back(points[var + 4]);
				yCoords.push_back(points[var + 7]);

				zCoords.push_back(points[var + 2]);
				zCoords.push_back(points[var + 5]);
				zCoords.push_back(points[var + 8]);

				texcords.push_back(0);
				texcords.push_back(0);
				texcords.push_back(1.0f);
				texcords.push_back(0.0f);
				texcords.push_back(0.5f);
				texcords.push_back(1.0f);
			}

			//std::cout << "Coords " << points.size() << std::endl;
			//std::cout << "Normals " << norms.size() << std::endl;

			const size_t numTris = points.size() / 3;
			//std::cout << "Triangles: " << numTris << std::endl;

			for (unsigned int itri = 0; itri < numTris; ++itri)
			{
				elements.push_back(itri);
			}

			//std::cout << "Elements " << elements.size() << std::endl;

			initBuffers(&elements, &points, &norms, &texcords, &tangents, &bitangents);
			computeBounds(points);
			_loaded = true;
		}
	}
	catch (const std::exception& e)
	{
		std::cout << "Exception in STLMesh::STLMesh\n" << e.what() << std::endl;
	}
}

bool STLMesh::loaded() const
{
	return _loaded;
}