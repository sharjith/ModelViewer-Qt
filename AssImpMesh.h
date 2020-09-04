#pragma once

#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "TriangleMesh.h"

using namespace std;

struct Vertex
{
    // Position
    glm::vec3 Position;
    // Normal
    glm::vec3 Normal;
    // TexCoords
    glm::vec2 TexCoords;
};

struct Texture
{
    GLuint id;
    string type;
    aiString path;
};

class AssImpMesh : public TriangleMesh
{
public:
    /*  Mesh Data  */
    vector<Vertex> vertices;
    vector<GLuint> indices;
    vector<Texture> textures;

    /*  Functions  */
    // Constructor
    AssImpMesh(QOpenGLShaderProgram* shader, vector<Vertex> vertices, vector<GLuint> indices, vector<Texture> textures ) : TriangleMesh(shader, "AssImpMesh")
    {
        this->vertices = vertices;
        this->indices = indices;
        this->textures = textures;

        // Now that we have all the required data, set the vertex buffers and its attribute pointers.
        this->setupMesh( );
    }

    // Render the mesh
    /*void render()
    {
        // Bind appropriate textures
        GLuint diffuseNr = 1;
        GLuint specularNr = 1;

        for( GLuint i = 0; i < this->textures.size( ); i++ )
        {
            glActiveTexture( GL_TEXTURE0 + i ); // Active proper texture unit before binding
            // Retrieve texture number (the N in diffuse_textureN)
            stringstream ss;
            string number;
            string name = this->textures[i].type;

            if( name == "texture_diffuse" )
            {
                ss << diffuseNr++; // Transfer GLuint to stream
            }
            else if( name == "texture_specular" )
            {
                ss << specularNr++; // Transfer GLuint to stream
            }

            number = ss.str( );
            // Now set the sampler to the correct texture unit
            _prog->bind();
            _prog->setUniformValue((name + number).c_str(), i);
            // And finally bind the texture
            glBindTexture( GL_TEXTURE_2D, this->textures[i].id );
        }

        // Also set each mesh's shininess property to a default value (if you want you could extend this to another mesh property and possibly change this value)
        _prog->setUniformValue("material.shininess", 16.0f );

        // Draw mesh
        glBindVertexArray( this->VAO );
        glDrawElements( GL_TRIANGLES, this->indices.size( ), GL_UNSIGNED_INT, 0 );
        glBindVertexArray( 0 );

        // Always good practice to set everything back to defaults once configured.
        for ( GLuint i = 0; i < this->textures.size( ); i++ )
        {
            glActiveTexture( GL_TEXTURE0 + i );
            glBindTexture( GL_TEXTURE_2D, 0 );
        }
    }*/

private:
    /*  Render data  */
    GLuint VAO, VBO, EBO;

    /*  Functions    */
    // Initializes all the buffer objects/arrays
    void setupMesh( )
    {
        std::vector<float> points;
        std::vector<float> normals;
        std::vector<float> texCoords;

        for(Vertex v : vertices)
        {
            points.push_back(v.Position.x);
            points.push_back(v.Position.y);
            points.push_back(v.Position.z);

            normals.push_back(v.Normal.x);
            normals.push_back(v.Normal.y);
            normals.push_back(v.Normal.z);

            texCoords.push_back(v.TexCoords.x);
            texCoords.push_back(v.TexCoords.y);
        }

        initBuffers(&indices, &points, &normals, &texCoords);
        computeBoundingSphere(points);
    }
};


