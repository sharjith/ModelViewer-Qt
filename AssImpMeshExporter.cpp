#include "AssImpMeshExporter.h"
#include <AssImpMesh.h>
#include <assimp/Exporter.hpp>

AssImpMeshExporter::AssImpMeshExporter(QObject *parent)
    : QObject{parent}
{

}

aiReturn AssImpMeshExporter::exportMeshes(const std::vector<AssImpMesh*>& meshes, const std::string& exportPath)
{
    std::vector<aiMesh*> aiMeshes;
    for(AssImpMesh* mesh : meshes)
    {
        std::vector<aiVector3D> aivertices;
        std::vector<unsigned int> aiindices;
        std::vector<aiVector3D> ainormals;
        std::vector<aiVector3D> aitexCoords;

        for(Vertex v : mesh->vertices())
        {
            aivertices.push_back(aiVector3D(v.Position.x, v.Position.y, v.Position.z));
            ainormals.push_back(aiVector3D(v.Normal.x, v.Normal.y, v.Normal.z));
            aitexCoords.push_back(aiVector3D(v.TexCoords.s, v.TexCoords.t, 0));
        }
        for(int index : mesh->indices())
        {
            aiindices.push_back(index);
        }

        aiMesh* aimesh = createMesh(aivertices, aiindices, ainormals, aitexCoords);
        aiMeshes.push_back(aimesh);
    }

    aiScene* scene = createScene(aiMeshes);
    // Export the scene to the selected file type
    Assimp::Exporter exporter;
    std::string fileExtension = exportPath.substr(exportPath.find_last_of('.') + 1);
    aiReturn result = exporter.Export(scene, fileExtension.c_str(), exportPath.c_str());

    if (result == aiReturn_SUCCESS) {
        std::cout << "Scene with multiple meshes exported successfully!" << std::endl;
    } else {
        std::cerr << "Error exporting scene: " << exporter.GetErrorString() << std::endl;
    }

    return result;
}

aiMesh* AssImpMeshExporter::createMesh(const std::vector<aiVector3D>& vertices,
                   const std::vector<unsigned int>& indices,
                   const std::vector<aiVector3D>& normals,
                   const std::vector<aiVector3D>& texCoords) {
    // Create a new aiMesh
    aiMesh* mesh = new aiMesh();

    // Set number of vertices and allocate memory for vertices
    mesh->mNumVertices = static_cast<unsigned int>(vertices.size());
    mesh->mVertices = new aiVector3D[mesh->mNumVertices];
    for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
        mesh->mVertices[i] = vertices[i];
    }

    // Set indices (faces)
    mesh->mNumFaces = static_cast<unsigned int>(indices.size()) / 3;
    mesh->mFaces = new aiFace[mesh->mNumFaces];
    for (unsigned int i = 0; i < mesh->mNumFaces; ++i) {
        mesh->mFaces[i].mNumIndices = 3;
        mesh->mFaces[i].mIndices = new unsigned int[3];
        for (unsigned int j = 0; j < 3; ++j) {
            mesh->mFaces[i].mIndices[j] = indices[i * 3 + j];
        }
    }

    // If you have normals, allocate and set them
    if (!normals.empty()) {
        mesh->mNormals = new aiVector3D[mesh->mNumVertices];
        for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
            mesh->mNormals[i] = normals[i];
        }
    }

    // If you have texture coordinates, allocate and set them
    if (!texCoords.empty()) {
        mesh->mTextureCoords[0] = new aiVector3D[mesh->mNumVertices];
        for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
            mesh->mTextureCoords[0][i] = texCoords[i];
        }
    }

    return mesh;
}

aiScene* AssImpMeshExporter::createScene(const std::vector<aiMesh*>& meshes)
{
    // Create an aiScene to hold the meshes
        aiScene* scene = new aiScene();
        scene->mNumMeshes = static_cast<unsigned int>(meshes.size());
        scene->mMeshes = new aiMesh*[scene->mNumMeshes];

        // Copy the mesh pointers to the scene
        for (unsigned int i = 0; i < scene->mNumMeshes; ++i) {
            scene->mMeshes[i] = meshes[i];
        }

        // Assign a material to each mesh (required for exporting in some formats)
        scene->mNumMaterials = 1;
        scene->mMaterials = new aiMaterial*[1];
        scene->mMaterials[0] = new aiMaterial();
        for (unsigned int i = 0; i < scene->mNumMeshes; ++i) {
            scene->mMeshes[i]->mMaterialIndex = 0; // Set material index for the mesh
        }

        // Create a root node and link all meshes
        scene->mRootNode = new aiNode();
        scene->mRootNode->mNumMeshes = scene->mNumMeshes;
        scene->mRootNode->mMeshes = new unsigned int[scene->mNumMeshes];
        for (unsigned int i = 0; i < scene->mNumMeshes; ++i) {
            scene->mRootNode->mMeshes[i] = i;  // Index of the mesh in the scene's mesh array
        }

        return scene;
}


