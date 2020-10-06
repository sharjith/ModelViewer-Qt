#include "AssImpMesh.h"

using namespace std;


/*  Functions  */
// Constructor
AssImpMesh::AssImpMesh(QOpenGLShaderProgram* shader, vector<Vertex> vertices, vector<unsigned int> indices, vector<Texture> textures, GLMaterial material) : TriangleMesh(shader, "AssImpMesh")
{
    _vertices = vertices;
    _indices = indices;
    _textures = textures;

    _material = material;
    // Now that we have all the required data, set the vertex buffers and its attribute pointers.
    setupMesh();
}

void AssImpMesh::render()
{
    if (!_vertexArrayObject.isCreated())
        return;

    setupTextures();
    setupUniforms();

    // Bind appropriate textures
    unsigned int diffuseNr = 1;
    unsigned int specularNr = 1;
    unsigned int emissiveNr = 1;
    unsigned int normalNr = 1;
    unsigned int heightNr = 1;
    unsigned int opacityNr = 1;

    for (unsigned int i = 0; i < _textures.size(); i++)
    {
        glActiveTexture(GL_TEXTURE10 + i); // Active proper texture unit before binding
        // Retrieve texture number (the N in diffuse_textureN)
        stringstream ss;
        string number;
        string name = _textures[i].type;

        // Transfer unsigned int to stream
        if (name == "texture_diffuse")
        {
            ss << diffuseNr++;
        }
        else if (name == "texture_specular")
        {
            ss << specularNr++;
        }
        else if (name == "texture_emissive")
        {
            ss << emissiveNr++;
        }
        else if (name == "texture_normal")
        {
            ss << normalNr++;
        }
        else if (name == "texture_height")
        {
            ss << heightNr++;
        }
        else if (name == "texture_opacity")
        {
            ss << opacityNr++;
        }
        number = ss.str();
        // Now set the sampler to the correct texture unit
        _prog->bind();
        _prog->setUniformValue((name + number).c_str(), i);
        // And finally bind the texture
        glBindTexture(GL_TEXTURE_2D, _textures[i].id);
    }

    if (_material.opacity() < 1.0f)
    {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_LINE_SMOOTH);
        glEnable(GL_POLYGON_SMOOTH);
    }
    else
    {
        glDisable(GL_BLEND);
    }

    _vertexArrayObject.bind();
    glDrawElements(GL_TRIANGLES, _nVerts, GL_UNSIGNED_INT, 0);
    _vertexArrayObject.release();
    _prog->release();
    glDisable(GL_BLEND);

    // Always good practice to set everything back to defaults once configured.
    for (unsigned int i = 0; i < _textures.size(); i++)
    {
        glActiveTexture(GL_TEXTURE10 + i);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}


/*  Functions    */
// Initializes all the buffer objects/arrays
void AssImpMesh::setupMesh()
{
    std::vector<float> points;
    std::vector<float> normals;
    std::vector<float> texCoords;
    std::vector<float> tangents;
    std::vector<float> bitangents;

    for (Vertex v : _vertices)
    {
        points.push_back(v.Position.x);
        points.push_back(v.Position.y);
        points.push_back(v.Position.z);

        normals.push_back(v.Normal.x);
        normals.push_back(v.Normal.y);
        normals.push_back(v.Normal.z);

        texCoords.push_back(v.TexCoords.x);
        texCoords.push_back(v.TexCoords.y);

        tangents.push_back(v.Tangent.x);
        tangents.push_back(v.Tangent.y);
        tangents.push_back(v.Tangent.z);
        bitangents.push_back(v.Bitangent.x);
        bitangents.push_back(v.Bitangent.y);
        bitangents.push_back(v.Bitangent.z);
    }

    _hasTexture = false;

    for (unsigned int i = 0; i < _textures.size(); i++)
    {
        string name = _textures[i].type;

        if (name == "texture_diffuse")
        {
            _hasDiffuseTexture = true;
        }
        else if (name == "texture_specular")
        {
            _hasSpecularTexture = true;
        }
        else if (name == "texture_emissive")
        {
            _hasEmissiveTexture = true;
        }
        else if (name == "texture_normal")
        {
            _hasNormalTexture = true;
        }
        else if (name == "texture_height")
        {
            _hasHeightTexture = true;
        }
        else if (name == "texture_opacity")
        {
            _hasOpacityTexture = true;
        }
    }

    initBuffers(&_indices, &points, &normals, &texCoords, &tangents, &bitangents);
    computeBounds();
}
