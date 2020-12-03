#include "AssImpMesh.h"

using namespace std;

/*  Functions  */
// Constructor
AssImpMesh::AssImpMesh(QOpenGLShaderProgram* shader, QString name, vector<Vertex> vertices, vector<unsigned int> indices, vector<Texture> textures, GLMaterial material) : TriangleMesh(shader, "AssImpMesh")
{
	setAutoIncrName(name);
	_vertices = vertices;
	_indices = indices;
	_textures = textures;
	/*for (Texture t : _textures)
	{
		std::cout << "AssImpMesh::AssImpMesh : texture = " << t.id << std::endl;
	}*/

	_material = material;
	// Now that we have all the required data, set the vertex buffers and its attribute pointers.
	setupMesh();
}

AssImpMesh::~AssImpMesh()
{
	if (_textures.size())
	{
		for (Texture t : _textures)
		{
			//std::cout << "AssImpMesh::~AssImpMesh : texture = " << t.id << std::endl;
			glDeleteTextures(1, &t.id);
		}
	}
}

TriangleMesh* AssImpMesh::clone()
{
	return new AssImpMesh(_prog, _name, _vertices, _indices, _textures, _material);
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
	// PBR
	unsigned int albedoNr = 1;
	unsigned int metallicNr = 1;
	unsigned int roughnessNr = 1;
	unsigned int normalPBRNr = 1;
	unsigned int aoNr = 1;
	
	bool hasTexture = false;
	// ADS
	for (unsigned int i = 0; i < _textures.size(); i++)
	{		
		hasTexture = false;
		// Retrieve texture number (the N in diffuse_textureN)
		stringstream ss;
		string number;
		string name = _textures[i].type;

		// Transfer unsigned int to stream
		if (name == "texture_diffuse")
		{
			ss << diffuseNr++;
			hasTexture = true;
		}
		if (name == "texture_specular")
		{
			ss << specularNr++;
			hasTexture = true;
		}
		if (name == "texture_emissive")
		{
			ss << emissiveNr++;
			hasTexture = true;
		}
		if (name == "texture_normal")
		{
			ss << normalNr++;
			hasTexture = true;
		}
		if (name == "texture_height")
		{
			ss << heightNr++;
			hasTexture = true;
		}
		if (name == "texture_opacity")
		{
			ss << opacityNr++;
			hasTexture = true;
		}		
		number = ss.str();

		if (hasTexture)
		{
			glActiveTexture(GL_TEXTURE10 + i); // Active proper texture unit before binding
			// Now set the sampler to the correct texture unit
			_prog->bind();
			_prog->setUniformValue((name + number).c_str(), i);
			// And finally bind the texture
			glBindTexture(GL_TEXTURE_2D, _textures[i].id);
		}
	}
	// PBR from ADS
	for (unsigned int i = 0; i < _textures.size(); i++)
	{
		hasTexture = false;
		// Retrieve texture number (the N in diffuse_textureN)
		stringstream ss;
		string number;
		string name = _textures[i].type;
		// Transfer unsigned int to stream
		if (name == "texture_diffuse")
		{
			ss << diffuseNr++;
			hasTexture = true;
		}
		if (name == "texture_specular")
		{
			ss << specularNr++;
			hasTexture = true;
		}		
		if (name == "texture_normal")
		{
			ss << normalNr++;
			hasTexture = true;
		}
		if (name == "texture_height")
		{
			ss << heightNr++;
			hasTexture = true;
		}
		if (name == "texture_opacity")
		{
			ss << opacityNr++;
			hasTexture = true;
		}
		number = ss.str();
		if (hasTexture)
		{
			glActiveTexture(GL_TEXTURE20 + i); // Active proper texture unit before binding
			// Now set the sampler to the correct texture unit
			_prog->bind();
			_prog->setUniformValue((name + number).c_str(), i);
			// And finally bind the texture
			glBindTexture(GL_TEXTURE_2D, _textures[i].id);
		}
	}
	// PBR from model
	for (unsigned int i = 0; i < _textures.size(); i++)
	{
		hasTexture = false;
		// Retrieve texture number (the N in diffuse_textureN)
		stringstream ss;
		string number;
		string name = _textures[i].type;
		// Transfer unsigned int to stream
		if (name == "albedoMap")
		{
			ss << albedoNr++;
			hasTexture = true;
		}
		if (name == "metallicMap")
		{
			ss << metallicNr++;
			hasTexture = true;
		}
		if (name == "roughnessMap")
		{
			ss << roughnessNr++;
			hasTexture = true;
		}
		if (name == "normalMap")
		{
			ss << normalPBRNr++;
			hasTexture = true;
		}		
		if (name == "aoMap")
		{
			ss << aoNr++;
			hasTexture = true;
		}
		number = ss.str();
		if (hasTexture)
		{
			glActiveTexture(GL_TEXTURE20 + i); // Active proper texture unit before binding
			// Now set the sampler to the correct texture unit
			_prog->bind();
			_prog->setUniformValue((name + number).c_str(), i);
			// And finally bind the texture
			glBindTexture(GL_TEXTURE_2D, _textures[i].id);
		}
	}

	if (_material.opacity() < 1.0f || _hasOpacityADSMap || _hasOpacityPBRMap)
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

	// Handle lighting normal for negative scaling
	if ((_scaleX < 0 && _scaleY > 0 && _scaleZ > 0) ||
		(_scaleX > 0 && _scaleY < 0 && _scaleZ > 0) ||
		(_scaleX > 0 && _scaleY > 0 && _scaleZ < 0) ||
		(_scaleX < 0 && _scaleY < 0 && _scaleZ < 0))
	{
		glFrontFace(GL_CW);
	}
	else
	{
		glFrontFace(GL_CCW);
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

	for (const Vertex& v : _vertices)
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
			_hasDiffuseADSMap = true;
			_hasAlbedoPBRMap = true;
		}
		if (name == "texture_specular")
		{
			_hasSpecularADSMap = true;
			_hasMetallicPBRMap = true;
		}
		if (name == "texture_emissive")
		{
			_hasEmissiveADSMap = true;
		}
		if (name == "texture_normal")
		{
			_hasNormalADSMap = true;
			_hasNormalPBRMap = true;
		}
		if (name == "texture_height")
		{
			_hasHeightADSMap = true;
			_hasHeightPBRMap = true;
		}
		if (name == "texture_opacity")
		{
			_hasOpacityADSMap = true;
			_hasOpacityPBRMap = true;
		}	

		// PBR from model
		if (name == "albedoMap")
		{			
			_hasAlbedoPBRMap = true;
		}
		if (name == "metallicMap")
		{
			_hasMetallicPBRMap = true;
		}
		if (name == "roughnessMap")
		{
			_hasRoughnessPBRMap = true;
		}
		if (name == "normalMap")
		{
			_hasNormalPBRMap = true;
		}
		if (name == "aoMap")
		{
			_hasAOPBRMap = true;
		}
	}

	initBuffers(&_indices, &points, &normals, &texCoords, &tangents, &bitangents);
	computeBounds();
}