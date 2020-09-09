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

#include "AssImpMesh.h"
#include "TriangleMesh.h"

using namespace std;


class AssImpModel : public TriangleMesh
{
public:
    /*  Functions   */
    // Constructor, expects a filepath to a 3D model.
    AssImpModel(QOpenGLShaderProgram* prog, GLchar *path ) : TriangleMesh(prog, "AssImpModel")
    {
        this->loadModel( path );
        QFileInfo f;
        f.setFile(QString(path));
        setName(f.baseName());
        BoundingSphere sph;        
        for ( GLuint i = 0; i < this->meshes.size(); i++ )
        {
            this->meshes[i]->setName(f.baseName() + QString("%1").arg(i));
            sph.addSphere(this->meshes[i]->getBoundingSphere());
        }
        _boundingSphere = sph;
    }

    // Draws the model, and thus all its meshes
    
    virtual void render()
    {
        for ( GLuint i = 0; i < this->meshes.size( ); i++ )
        {
            this->meshes[i]->render();
        }
    }   

    vector<AssImpMesh*> getMeshes() const
    {
        return meshes;
    }

private:
    /*  Model Data  */
    vector<AssImpMesh*> meshes;
    string directory;
    vector<Texture> textures_loaded;	// Stores all the textures loaded so far, optimization to make sure textures aren't loaded more than once.

    /*  Functions   */
    // Loads a model with supported ASSIMP extensions from file and stores the resulting meshes in the meshes vector.
    void loadModel( string path )
    {
        // Read file via ASSIMP
        Assimp::Importer importer;
        const aiScene *scene = importer.ReadFile( path, aiProcessPreset_TargetRealtime_Fast);

        // Check for errors
        if( !scene || scene->mFlags == AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode ) // if is Not Zero
        {
            cout << "ERROR::ASSIMP:: " << importer.GetErrorString( ) << endl;
            return;
        }
        // Retrieve the directory path of the filepath
        this->directory = path.substr( 0, path.find_last_of( '/' ) );

        // Process ASSIMP's root node recursively
        this->processNode( scene->mRootNode, scene );
    }

    // Processes a node in a recursive fashion. Processes each individual mesh located at the node and repeats this process on its children nodes (if any).
    void processNode( aiNode* node, const aiScene* scene )
    {
        // Process each mesh located at the current node
        for ( GLuint i = 0; i < node->mNumMeshes; i++ )
        {
            // The node object only contains indices to index the actual objects in the scene.
            // The scene contains all the data, node is just to keep stuff organized (like relations between nodes).
            aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];

            this->meshes.push_back( this->processMesh( mesh, scene ) );
        }

        // After we've processed all of the meshes (if any) we then recursively process each of the children nodes
        for ( GLuint i = 0; i < node->mNumChildren; i++ )
        {
            this->processNode( node->mChildren[i], scene );
        }
    }

    AssImpMesh* processMesh( aiMesh *mesh, const aiScene *scene )
    {
        // Data to fill
        vector<Vertex> vertices;
        vector<GLuint> indices;
        vector<Texture> textures;

        // Walk through each of the mesh's vertices
        int step = 0;
        for ( GLuint i = 0; i < mesh->mNumVertices; i++ )
        {
            step++;
            Vertex vertex;
            glm::vec3 vector; // We declare a placeholder vector since assimp uses its own vector class that doesn't directly convert to glm's vec3 class so we transfer the data to this placeholder glm::vec3 first.

            // Positions
            vector.x = mesh->mVertices[i].x;
            vector.y = mesh->mVertices[i].y;
            vector.z = mesh->mVertices[i].z;
            vertex.Position = vector;

            // Normals
            vector.x = mesh->mNormals[i].x;
            vector.y = mesh->mNormals[i].y;
            vector.z = mesh->mNormals[i].z;
            vertex.Normal = vector;

            // Texture Coordinates
            if( mesh->mTextureCoords[0] ) // Does the mesh contain texture coordinates?
            {
                glm::vec2 vec;
                // A vertex can contain up to 8 different texture coordinates. We thus make the assumption that we won't
                // use models where a vertex can have multiple texture coordinates so we always take the first set (0).
                vec.x = mesh->mTextureCoords[0][i].x;
                vec.y = mesh->mTextureCoords[0][i].y;
                vertex.TexCoords = vec;
            }
            else
            {                
                if (step == 1)
                    vertex.TexCoords = glm::vec2(0.0f, 0.0f);
                else if (step == 2)
                    vertex.TexCoords = glm::vec2(1.0f, 0.0f);
                else
                {
                    vertex.TexCoords = glm::vec2(0.5f, 1.0f);
                    step = 0;
                }

            }
            vertices.push_back( vertex );
        }

        // Now wak through each of the mesh's faces (a face is a mesh its triangle) and retrieve the corresponding vertex indices.
        for ( GLuint i = 0; i < mesh->mNumFaces; i++ )
        {
            aiFace face = mesh->mFaces[i];
            // Retrieve all indices of the face and store them in the indices vector
            for ( GLuint j = 0; j < face.mNumIndices; j++ )
            {
                indices.push_back( face.mIndices[j] );
            }
        }


        // Process materials
		GLMaterialProps materials = {
			{ 0.2109375f, 0.125f, 0.05078125f, 1.0f },      // ambient 54 32 13 
			{ 0.7109375f, 0.62890625f, 0.55078125f, 1.0f }, // diffuse 182 161 141
			{ 0.37890625f, 0.390625f, 0.3359375f, 1.0f },   // specular 97 100 86
			{1.0f, 1.0f, 1.0f, 1.0f},   // specref
			{ 0.0f, 0.0f, 0.0f, 1.0f }, // emissive
			128 * 0.2f, // shininess
			1.0f,   // opacity
            false, // metallic
            false // texture
		};
        if( mesh->mMaterialIndex != 0 )
        {
            aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
            // We assume a convention for sampler names in the shaders. Each diffuse texture should be named
            // as 'texture_diffuseN' where N is a sequential number ranging from 1 to MAX_SAMPLER_NUMBER.
            // Same applies to other texture as the following list summarizes:
            // Diffuse: texture_diffuseN
            // Specular: texture_specularN
            // Normal: texture_normalN

            // 1. Diffuse maps
            vector<Texture> diffuseMaps = this->loadMaterialTextures( material, aiTextureType_DIFFUSE, "texture_diffuse" );
            textures.insert( textures.end( ), diffuseMaps.begin( ), diffuseMaps.end( ) );

            // 2. Specular maps
            vector<Texture> specularMaps = this->loadMaterialTextures( material, aiTextureType_SPECULAR, "texture_specular" );
            textures.insert( textures.end( ), specularMaps.begin( ), specularMaps.end( ) );

            aiColor3D color(0.f, 0.f, 0.f);
            float opacity = 1.0f;
            material->Get(AI_MATKEY_OPACITY, opacity);            
            if (AI_SUCCESS == material->Get(AI_MATKEY_COLOR_AMBIENT, color))
            {
                materials.ambientMaterial = QVector4D(color.r, color.g, color.b, opacity);
            }
            if (AI_SUCCESS == material->Get(AI_MATKEY_COLOR_DIFFUSE, color))
            {
                materials.diffuseMaterial = QVector4D(color.r, color.g, color.b, opacity);
            }
            if (AI_SUCCESS == material->Get(AI_MATKEY_COLOR_SPECULAR, color))
            {
                materials.specularMaterial = QVector4D(color.r, color.g, color.b, opacity);
            }
            if (AI_SUCCESS == material->Get(AI_MATKEY_COLOR_EMISSIVE, color))
            {
                materials.emmissiveMaterial = QVector4D(color.r, color.g, color.b, opacity);
            }
        }       

        // Return a mesh object created from the extracted mesh data
        return new AssImpMesh(_prog, vertices, indices, textures, materials);
    }

    // Checks all material textures of a given type and loads the textures if they're not loaded yet.
    // The required info is returned as a Texture struct.
    vector<Texture> loadMaterialTextures( aiMaterial *mat, aiTextureType type, string typeName )
    {
        vector<Texture> textures;

        for ( GLuint i = 0; i < mat->GetTextureCount( type ); i++ )
        {
            aiString str;
            mat->GetTexture( type, i, &str );

            // Check if texture was loaded before and if so, continue to next iteration: skip loading a new texture
            GLboolean skip = false;

            for ( GLuint j = 0; j < textures_loaded.size( ); j++ )
            {
                if( textures_loaded[j].path == str )
                {
                    textures.push_back( textures_loaded[j] );
                    skip = true; // A texture with the same filepath has already been loaded, continue to next one. (optimization)

                    break;
                }
            }

            if( !skip )
            {
                // If texture hasn't been loaded already, load it
                Texture texture;
                texture.id = textureFromFile( str.C_Str( ), this->directory );
                texture.type = typeName;
                texture.path = str;
                textures.push_back( texture );

                this->textures_loaded.push_back( texture );  // Store it as texture loaded for entire model, to ensure we won't unnecesery load duplicate textures.
            }
        }

        return textures;
    }

    GLint textureFromFile( const char *path, string directory )
    {
        //Generate texture ID and load texture data
        string filename = string( path );
        filename = directory + '/' + filename;
        GLuint textureID;
        glGenTextures( 1, &textureID );

        QImage texImage;

        if (!texImage.load(QString(filename.c_str( ))))
        { // Load first image from file
            qWarning("Could not read image file, using single-color instead.");
            QImage dummy(128, 128, static_cast<QImage::Format>(5));
            dummy.fill(Qt::white);
            texImage = dummy;
        }
        else
        {
            texImage = QGLWidget::convertToGLFormat(texImage); // flipped 32bit RGBA
        }

        // Assign texture to ID
        glBindTexture( GL_TEXTURE_2D, textureID );
        glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA, texImage.width(), texImage.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, texImage.bits() );
        glGenerateMipmap( GL_TEXTURE_2D );

        // Parameters
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture( GL_TEXTURE_2D, 0 );

        return textureID;
    }
};

