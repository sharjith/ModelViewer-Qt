#pragma once

#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <map>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <QImage>
#include <QString>
#include <QFileInfo>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/ProgressHandler.hpp>

#include "AssImpMesh.h"
#include "TriangleMesh.h"


class AssImpModelProgressHandler : public QObject, public Assimp::ProgressHandler
{
	Q_OBJECT
public:
	virtual bool Update(float percentage);

	// Required by Qt system. TODO: Make sure it is fine
	inline void* operator new(size_t, void* ptr) noexcept
    {
        return ptr;
    }

    using Assimp::ProgressHandler::operator new;

signals:
	void fileReadProcessed(float percent);
};

class AssImpModelLoader : public QObject, public QOpenGLFunctions_4_5_Core
{
	Q_OBJECT
public:
	/*  Functions   */
	// Constructor, expects a filepath to a 3D model.
	AssImpModelLoader(QOpenGLShaderProgram* prog);

	~AssImpModelLoader();

	/*  Functions   */
	// Loads a model with supported ASSIMP extensions from file and stores the resulting meshes in the meshes vector.
	void loadModel(std::string path);

	std::vector<AssImpMesh*> getMeshes() const;

	QString getErrorMessage() const;

signals:
	void fileReadProcessed(float percent);
	void verticesProcessed(float percent);
	void nodeProcessed(int nodeNum, int totalNodes);
	void loadingCancelled();

public slots:
	void processFileReadProgress(float percentage);
	void cancelLoading();

private:
	QOpenGLShaderProgram* _prog;
	std::string _path;
	/*  Model Data  */
	std::vector<AssImpMesh*> _meshes;
	std::string directory;
	std::vector<Texture> _loadedTextures;	// Stores all the textures loaded so far, optimization to make sure textures aren't loaded more than once.

	// Processes a node in a recursive fashion. Processes each individual mesh located at the node and repeats this process on its children nodes (if any).
	void processNode(int nodeNum, aiNode* node, const aiScene* scene);

	AssImpMesh* processMesh(aiMesh* mesh, const aiScene* scene);

	// Checks all material textures of a given type and loads the textures if they're not loaded yet.
	// The required info is returned as a Texture struct.
	std::vector<Texture> loadMaterialTextures(aiMaterial* mat, aiTextureType type, std::string typeName);

	unsigned int textureFromFile(const char* path, std::string directory);

	Assimp::Importer _importer;
	AssImpModelProgressHandler* _progHandler;
	QString _errorMessage;
	bool _loadingCancelled;
};
