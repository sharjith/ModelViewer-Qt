#include "AssImpMesh.h"

#include <QDataStream>
#include <QFileInfo>
#include <QImage>
#include <QVariantMap>

#include <meshoptimizer.h>

using namespace std;

bool AssImpMesh::_currentBlendEnabled;
GLenum AssImpMesh::_currentFrontFace;

/*  Functions  */
// Constructor
AssImpMesh::AssImpMesh(QOpenGLShaderProgram* shader, QString name, vector<Vertex> vertices, vector<unsigned int> indices, vector<GLMaterial::Texture> textures, GLMaterial material) : TriangleMesh(shader, "AssImpMesh")
{
	_currentBoundShader = nullptr;
	_currentBlendEnabled = false;
	_currentFrontFace = GL_CCW;
	//setAutoIncrName(name);
	_name = name;
	_vertices = vertices;
	_indices = indices;
	_textures = textures;
	_material = material;

	// Optimize the mesh (reorder indices and vertices for better vertex cache locality, overdraw, and vertex fetch)
	optimizeMesh();

	// Now that we have all the required data, set the vertex buffers and its attribute pointers.
	setupMesh();
}

AssImpMesh::~AssImpMesh()
{
	/*if (_textures.size())
	{
		for (const GLMaterial::Texture &t : _textures)
		{
			glDeleteTextures(1, &t.id);
		}
	}*/
	releaseCurrentShader();
}

TriangleMesh* AssImpMesh::clone()
{
	return new AssImpMesh(_prog, _name, _vertices, _indices, _textures, _material);
}


void AssImpMesh::render()
{
	if (!_vertexArrayObject.isCreated())
		return;

	// Smart shader binding - only bind if different
	bool shaderChanged = false;
	if (_currentBoundShader != _prog)
	{
		if (_currentBoundShader)
		{
			_currentBoundShader->release();
		}
		_prog->bind();
		_currentBoundShader = _prog;
		shaderChanged = true;

		// When shader changes, we need to recache texture bindings
		_textureBindingsDirty = true;
		_uniformsDirty = true;
	}

	cacheTextureBindings();

	if (/*shaderChanged ||*/ _uniformsDirty)
	{
		setupUniformsOptimized();
		_uniformsDirty = false;
	}

	// Bind textures efficiently
	bindTexturesOptimized();

	// Set render state efficiently
	setRenderStateOptimized();

	// Transparent draws must NOT write depth, but should still depth-test.
	GLboolean prevDepthMask = GL_TRUE;
	glGetBooleanv(GL_DEPTH_WRITEMASK, &prevDepthMask);
	if (isTransparent() && needsDepthMaskOff()) glDepthMask(GL_FALSE);

	_vertexArrayObject.bind();

	// Adjust vertex count based on primitive mode
	GLsizei drawCount = _nVerts;

	// For point rendering, use point size
	if (_primitiveMode == GL_POINTS)
	{
		glEnable(GL_PROGRAM_POINT_SIZE);
		glPointSize(3.0f);
	}

	// For line rendering, use line width
	if (_primitiveMode == GL_LINES || _primitiveMode == GL_LINE_STRIP || _primitiveMode == GL_LINE_LOOP)
	{
		glLineWidth(1.5f);
	}

	// Draw the mesh
	glDrawElements(_primitiveMode, drawCount, GL_UNSIGNED_INT, nullptr);

	// Reset point size
	if (_primitiveMode == GL_POINTS)
	{
		glDisable(GL_PROGRAM_POINT_SIZE);
	}

	_vertexArrayObject.release();

	if (isTransparent()) glDepthMask(prevDepthMask); // restore immediately

	_prog->release();

}

void AssImpMesh::optimizeMesh()
{
	// ============================================
	// MESH OPTIMIZATION (before splitting arrays)
	// ============================================
	// Check if this is a valid triangle mesh
	if (_indices.empty() || (_indices.size() % 3 != 0))
	{
		// Not a triangle mesh - skip meshoptimizer
		return;
	}
	if (_indices.size() > 300 && _vertices.size() > 100)
	{
		//qDebug("Optimizing mesh %s with %zu vertices and %zu indices...", qPrintable(_name), _vertices.size(), _indices.size());
		size_t vertexCount = _vertices.size();

		// Extract positions temporarily for overdraw optimization
		std::vector<float> tempPositions(vertexCount * 3);
		for (size_t i = 0; i < vertexCount; i++)
		{
			tempPositions[i * 3 + 0] = _vertices[i].Position.x;
			tempPositions[i * 3 + 1] = _vertices[i].Position.y;
			tempPositions[i * 3 + 2] = _vertices[i].Position.z;
		}

		// Step 1: Vertex Cache Optimization
		meshopt_optimizeVertexCache(
			_indices.data(),
			_indices.data(),
			_indices.size(),
			vertexCount
		);

		// Step 2: Overdraw Optimization
		meshopt_optimizeOverdraw(
			_indices.data(),
			_indices.data(),
			_indices.size(),
			tempPositions.data(),
			vertexCount,
			sizeof(float) * 3,
			1.05f
		);

		// Step 3: Vertex Fetch Optimization
		meshopt_optimizeVertexFetch(
			_vertices.data(),
			_indices.data(),
			_indices.size(),
			_vertices.data(),
			vertexCount,
			sizeof(Vertex)
		);
	}
}

/*  Functions    */
// Initializes all the buffer objects/arrays
void AssImpMesh::setupMesh()
{
	// ============================================
	// Extract to separate arrays
	// ============================================
	std::vector<float> points;
	std::vector<float> normals;
	std::vector<float> colors;
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

		colors.reserve(_vertices.size() * 4);
		colors.push_back(v.Color.r);
		colors.push_back(v.Color.g);
		colors.push_back(v.Color.b);
		colors.push_back(v.Color.a);

		// Extract all 4 texCoord sets
		for (int i = 0; i < 4; i++)
		{
			texCoords.push_back(v.TexCoords[i].x);
			texCoords.push_back(v.TexCoords[i].y);
		}

		tangents.push_back(v.Tangent.x);
		tangents.push_back(v.Tangent.y);
		tangents.push_back(v.Tangent.z);

		bitangents.push_back(v.Bitangent.x);
		bitangents.push_back(v.Bitangent.y);
		bitangents.push_back(v.Bitangent.z);
	}

	// ============================================
	// Texture flags
	// ============================================
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
			_hasEmissivePBRMap = true;
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
			_hasSpecularADSMap = true;
			_hasMetallicPBRMap = true;
		}
		if (name == "emissiveMap")
		{
			_hasEmissiveADSMap = true;
			_hasEmissivePBRMap = true;
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
		if (name == "heightMap")
		{
			_hasHeightPBRMap = true;
		}
		if (name == "opacityMap")
		{
			_hasOpacityPBRMap = true;
			_hasOpacityADSMap = true;
		}
		if (name == "transmissionMap")
		{
			_hasTransmissionPBRMap = true;
		}
		if (name == "iorMap")
		{
			_hasIORPBRMap = true;
		}
		if (name == "sheenColorMap")
		{
			_hasSheenColorPBRMap = true;
		}
		if (name == "sheenRoughnessMap")
		{
			_hasSheenRoughnessPBRMap = true;
		}
		if (name == "clearcoatColorMap")
		{
			_hasClearcoatPBRMap = true;
		}
		if (name == "clearcoatRoughnessMap")
		{
			_hasClearcoatRoughnessPBRMap = true;
		}
		if (name == "clearcoatNormalMap")
		{
			_hasClearcoatNormalPBRMap = true;
		}
	}

	size_t numVertices = _vertices.size();
	qDebug() << "Mesh name:" << _name;
	qDebug() << "=== BUFFER LAYOUT DEBUG ===";
	qDebug() << "Num vertices:" << numVertices;
	qDebug() << "Num floats in texCoords vector:" << texCoords.size();
	qDebug() << "Expected (4 sets x 2 floats x numVertices):" << 4 * 2 * numVertices;
	qDebug() << "First 16 texCoord values:";
	for (int i = 0; i < std::min(16, (int)texCoords.size()); i++)
	{
		qDebug() << "  [" << i << "]:" << texCoords[i];
	}
	// output the texture metadata for debugging
	qDebug() << "=== TEXTURE METADATA DEBUG ===";
	for (size_t i = 0; i < _textures.size(); i++)
	{
		const GLMaterial::Texture& tex = _textures[i];
		qDebug() << "Texture [" << i << "] Type:" << QString::fromStdString(tex.type)
			<< " ID:" << tex.id
			<< " HasAlpha:" << tex.hasAlpha
			<< " TexCoordIndex:" << tex.texCoordIndex
			<< " Scale:" << tex.scale.x << "," << tex.scale.y
			<< " Offset:" << tex.offset.x << "," << tex.offset.y
			<< " Rotation (radians):" << tex.rotation
			<< " WrapS:" << tex.wrapS
			<< " WrapT:" << tex.wrapT;

	}

	initBuffers(&_indices, &points, &normals, &colors, &texCoords, &tangents, &bitangents);
	computeBounds();
}

void AssImpMesh::cacheTextureBindings()
{
	if (!_textureBindingsDirty) return;

	_textureBindings.clear();
	_textureBindings.reserve(_textures.size() * 2); // Account for duplicates

	// Counters for numbering
	int diffuseNr = 1, specularNr = 1, emissiveNr = 1, normalNr = 1;
	int heightNr = 1, opacityNr = 1, albedoNr = 1, metallicNr = 1;
	int roughnessNr = 1, normalPBRNr = 1, aoNr = 1;
	int transmissionNr = 1, iorNr = 1;
	int sheenColorNr = 1, sheenRoughnessNr = 1;
	int clearcoatNr = 1, clearcoatRoughnessNr = 1, clearcoatNormalNr = 1;
	// New glTF extension counters
	int specularFactorNr = 1, specularColorNr = 1;
	int anisotropyNr = 1;
	int iridescenceNr = 1, iridescenceThicknessNr = 1;
	int thicknessNr = 1;
	int diffuseSpecGlossNr = 1, specularGlossinessNr = 1;

	for (size_t i = 0; i < _textures.size(); ++i)
	{
		const auto& texture = _textures[i];

		// Helper lambda to add binding
		auto addBinding = [&](const std::string& uniformName, GLuint unit) {
			PrecomputedTexture binding;
			binding.textureId = texture.id;
			binding.textureUnit = unit;
			binding.uniformLocation = _prog->uniformLocation(uniformName.c_str());
			binding.isValid = (binding.uniformLocation != -1);
			if (binding.isValid)
			{
				_textureBindings.push_back(binding);
			}
			};

		// Handle different texture types
		if (texture.type == "texture_diffuse")
		{
			addBinding("texture_diffuse" /*+ std::to_string(diffuseNr)*/, GL_TEXTURE10);
			addBinding("albedoMap" /*+ std::to_string(diffuseNr)*/, GL_TEXTURE10); // PBR duplicate
			diffuseNr++;
			_hasTextureAlpha = texture.hasAlpha;
		}
		else if (texture.type == "albedoMap")
		{
			addBinding("texture_diffuse" /*+ std::to_string(diffuseNr)*/, GL_TEXTURE10);
			addBinding("albedoMap" /*+ std::to_string(albedoNr)*/, GL_TEXTURE10);
			albedoNr++;
			_hasTextureAlpha = texture.hasAlpha;
		}
		else if (texture.type == "texture_specular")
		{
			addBinding("texture_specular" /*+ std::to_string(specularNr)*/, GL_TEXTURE11);
			addBinding("metallicMap" /*+ std::to_string(specularNr)*/, GL_TEXTURE11);
			specularNr++;
		}
		else if (texture.type == "metallicMap")
		{
			addBinding("texture_specular" /*+ std::to_string(specularNr)*/, GL_TEXTURE11);
			addBinding("metallicMap" /*+ std::to_string(metallicNr)*/, GL_TEXTURE11);
			metallicNr++;
		}
		else if (texture.type == "texture_emissive")
		{
			addBinding("texture_emissive" /*+ std::to_string(emissiveNr)*/, GL_TEXTURE12);
			addBinding("emissiveMap" /*+ std::to_string(emissiveNr)*/, GL_TEXTURE12);
			emissiveNr++;
		}
		else if (texture.type == "emissiveMap")
		{
			addBinding("texture_emissive" /*+ std::to_string(emissiveNr)*/, GL_TEXTURE12);
			addBinding("emissiveMap" /*+ std::to_string(emissiveNr)*/, GL_TEXTURE12);
			emissiveNr++;
		}
		else if (texture.type == "texture_normal")
		{
			addBinding("texture_normal" /*+ std::to_string(normalNr)*/, GL_TEXTURE13);
			addBinding("normalMap" /*+ std::to_string(normalNr)*/, GL_TEXTURE13);
			normalNr++;
		}
		else if (texture.type == "normalMap")
		{
			addBinding("normalMap" /*+ std::to_string(normalNr)*/, GL_TEXTURE13);
			normalNr++;
		}
		else if (texture.type == "texture_height")
		{
			addBinding("texture_height" /*+ std::to_string(heightNr)*/, GL_TEXTURE14);
			addBinding("heightMap" /*+ std::to_string(heightNr)*/, GL_TEXTURE14);
			heightNr++;
		}
		else if (texture.type == "heightMap")
		{
			addBinding("texture_height" /*+ std::to_string(heightNr)*/, GL_TEXTURE14);
			addBinding("heightMap" /*+ std::to_string(heightNr)*/, GL_TEXTURE14);
			heightNr++;
		}
		else if (texture.type == "texture_opacity")
		{
			addBinding("texture_opacity" /*+ std::to_string(opacityNr)*/, GL_TEXTURE15);
			addBinding("opacityMap" /*+ std::to_string(opacityNr)*/, GL_TEXTURE15);
			opacityNr++;
		}
		else if (texture.type == "opacityMap")
		{
			addBinding("texture_opacity" /*+ std::to_string(opacityNr)*/, GL_TEXTURE15);
			addBinding("opacityMap" /*+ std::to_string(opacityNr)*/, GL_TEXTURE15);
			opacityNr++;
		}
		else if (texture.type == "roughnessMap")
		{
			addBinding("roughnessMap" /*+ std::to_string(roughnessNr)*/, GL_TEXTURE16);
			roughnessNr++;
		}
		else if (texture.type == "aoMap" || texture.type == "occlusionMap")
		{
			addBinding("aoMap" /*+ std::to_string(aoNr)*/, GL_TEXTURE17);
			aoNr++;
		}
		else if (texture.type == "transmissionMap")
		{
			addBinding("transmissionMap" /*+ std::to_string(transmissionNr)*/, GL_TEXTURE18);
			transmissionNr++;
		}
		else if (texture.type == "iorMap")
		{
			addBinding("iorMap" /*+ std::to_string(iorNr)*/, GL_TEXTURE19);
			iorNr++;
		}
		else if (texture.type == "sheenColorMap")
		{
			addBinding("sheenColorMap" /*+ std::to_string(sheenColorNr)*/, GL_TEXTURE20);
			sheenColorNr++;
		}
		else if (texture.type == "sheenRoughnessMap")
		{
			addBinding("sheenRoughnessMap" /*+ std::to_string(sheenRoughnessNr)*/, GL_TEXTURE21);
			sheenRoughnessNr++;
		}
		else if (texture.type == "clearcoatColorMap")
		{
			addBinding("clearcoatColorMap" /*+ std::to_string(clearcoatNr)*/, GL_TEXTURE22);
			clearcoatNr++;
		}
		else if (texture.type == "clearcoatRoughnessMap")
		{
			addBinding("clearcoatRoughnessMap" /*+ std::to_string(clearcoatRoughnessNr)*/, GL_TEXTURE23);
			clearcoatRoughnessNr++;
		}
		else if (texture.type == "clearcoatNormalMap")
		{
			addBinding("clearcoatNormalMap" /*+ std::to_string(clearcoatNormalNr)*/, GL_TEXTURE24);
			clearcoatNormalNr++;
		}
		// === NEW GLTF EXTENSION TEXTURES ===
		else if (texture.type == "specularFactorMap")
		{
			addBinding("specularFactorMap" /*+ std::to_string(specularFactorNr)*/, GL_TEXTURE25);
			specularFactorNr++;
		}
		else if (texture.type == "specularColorMap")
		{
			addBinding("specularColorMap" /*+ std::to_string(specularColorNr)*/, GL_TEXTURE26);
			specularColorNr++;
		}
		else if (texture.type == "diffuseMap") // === KHR_materials_pbrSpecularGlossiness ===
		{
			addBinding("diffuseMap" /*+ std::to_string(diffuseSpecGlossNr)*/, GL_TEXTURE10);
			diffuseSpecGlossNr++;
			_hasTextureAlpha = texture.hasAlpha;
		}
		else if (texture.type == "specularGlossinessMap")
		{
			// GL_TEXTURE25 is reused (mutually exclusive with specularFactorMap)
			// The shader checks hasSpecularGlossinessMap to determine which to use
			addBinding("specularGlossinessMap" /*+ std::to_string(specularGlossinessNr)*/, GL_TEXTURE25);
			specularGlossinessNr++;
		}
		else if (texture.type == "anisotropyMap")
		{
			addBinding("anisotropyMap" /*+ std::to_string(anisotropyNr)*/, GL_TEXTURE27);
			anisotropyNr++;
		}
		else if (texture.type == "iridescenceMap")
		{
			addBinding("iridescenceMap" /*+ std::to_string(iridescenceNr)*/, GL_TEXTURE28);
			iridescenceNr++;
		}
		else if (texture.type == "iridescenceThicknessMap")
		{
			addBinding("iridescenceThicknessMap" /*+ std::to_string(iridescenceThicknessNr)*/, GL_TEXTURE29);
			iridescenceThicknessNr++;
		}
		else if (texture.type == "thicknessMap")
		{
			addBinding("thicknessMap" /*+ std::to_string(thicknessNr)*/, GL_TEXTURE30);
			thicknessNr++;
		}
		else if (texture.type == "diffuseTransmissionMap")
		{
			addBinding("diffuseTransmissionMap", GL_TEXTURE31);
		}
		else if (texture.type == "diffuseTransmissionColorMap")
		{
			addBinding("diffuseTransmissionColorMap", GL_TEXTURE9);
		}
	}

	_textureBindingsDirty = false;
}


void AssImpMesh::bindTexturesOptimized()
{
	for (const auto& binding : _textureBindings)
	{
		if (binding.isValid)
		{
			glActiveTexture(binding.textureUnit);
			glBindTexture(GL_TEXTURE_2D, binding.textureId);
			glUniform1i(binding.uniformLocation, binding.textureUnit - GL_TEXTURE0);
		}
	}
}

void AssImpMesh::setRenderStateOptimized()
{
	// Front face correction
	GLenum frontFace = GL_CCW;
	const int neg = (_scaleX < 0) + (_scaleY < 0) + (_scaleZ < 0);
	if (neg == 1 || neg == 3) frontFace = GL_CW;
	if (frontFace != _currentFrontFace)
	{
		glFrontFace(frontFace);
		_currentFrontFace = frontFace;
	}
}


void AssImpMesh::setupUniformsOptimized()
{
	if (!_uniformsDirty) return;

	// Only call the expensive setupUniforms when actually needed
	setupUniforms();
	_uniformsDirty = false;
}


// Convert a QString to aiString helper
static aiString qstrToAiString(const QString& s)
{
	// aiString stores a copy internally
	return aiString(s.toUtf8().constData());
}

void AssImpMesh::syncTexturesFromMaterialIfNeeded()
{
	// If mesh already has explicit paths for textures, do nothing
	bool hasAnyPath = false;
	for (const GLMaterial::Texture& t : _textures)
	{
		if (!QString::fromUtf8(t.path).isEmpty()) { hasAnyPath = true; break; }
	}
	if (hasAnyPath) return;

	// Use material->toVariantMap() so we don't need private-member access.
	QVariantMap vm = _material.toVariantMap();

	// Helper lambda to add a texture entry if path exists and file seems plausible.
	auto pushIfPresent = [&](const QString& matKey, const std::string& outType) {
		QVariant v = vm.value(matKey);
		if (!v.isValid()) return;
		QString path = v.toString().trimmed();
		if (path.isEmpty()) return;

		// make path absolute attempt is caller's responsibility; we just store what material had.
		GLMaterial::Texture t;
		t.id = 0;
		t.type = outType;
		t.path = path.toStdString();

		// CRITICAL: Copy sampler values from material's texture array
		// Find the matching texture type in material and copy its sampler settings
		auto matTexType = GLMaterial::stringToTextureType(QString::fromStdString(outType));
		if (matTexType != GLMaterial::TextureType::Count)
		{
			const auto& matTex = _material.texture(matTexType);
			t.wrapS = matTex.wrapS;
			t.wrapT = matTex.wrapT;
			t.magFilter = matTex.magFilter;
			t.minFilter = matTex.minFilter;
			t.texCoordIndex = matTex.texCoordIndex;
			t.scale = matTex.scale;
			t.offset = matTex.offset;
			t.rotation = matTex.rotation;
		}

		// Optionally detect alpha channel (light-weight check)
		bool hasAlpha = false;
		GLuint id = createGLTextureFromFile(path, hasAlpha);
		t.id = id;
		t.hasAlpha = hasAlpha;

		_textures.push_back(t);
		qDebug() << "syncTexturesFromMaterialIfNeeded: added texture type=" << QString::fromStdString(t.type)
			<< " path=" << QString::fromUtf8(t.path) << " hasAlpha=" << hasAlpha;
		};

	// Map GLMaterial variant keys -> mesh texture type strings used throughout AssImpMesh
	// (these type strings match the checks in setupMesh()/cacheTextureBindings)
	pushIfPresent("albedoMapPath", "albedoMap");        // PBR albedo
	pushIfPresent("normalMapPath", "normalMap");        // normal
	pushIfPresent("metallicMapPath", "metallicMap");      // metallic
	pushIfPresent("roughnessMapPath", "roughnessMap");     // roughness
	pushIfPresent("aoMapPath", "aoMap");            // ao / lightmap
	pushIfPresent("occlusionMapPath", "aoMap");            // ao / lightmap
	pushIfPresent("emissiveMapPath", "emissiveMap");      // emissive
	pushIfPresent("opacityMapPath", "opacityMap");       // opacity/alpha
	pushIfPresent("heightMapPath", "heightMap");        // height
	pushIfPresent("transmissionMapPath", "transmissionMap");  // transmission
	pushIfPresent("iorMapPath", "iorMap");
	pushIfPresent("sheenColorMapPath", "sheenColorMap");
	pushIfPresent("sheenRoughnessMapPath", "sheenRoughnessMap");
	pushIfPresent("clearcoatColorMapPath", "clearcoatColorMap");
	pushIfPresent("clearcoatRoughnessMapPath", "clearcoatRoughnessMap");
	pushIfPresent("clearcoatNormalMapPath", "clearcoatNormalMap");
	// New glTF extension textures
	pushIfPresent("specularFactorMapPath", "specularFactorMap");
	pushIfPresent("specularColorMapPath", "specularColorMap");
	pushIfPresent("anisotropyMapPath", "anisotropyMap");
	pushIfPresent("iridescenceMapPath", "iridescenceMap");
	pushIfPresent("iridescenceThicknessMapPath", "iridescenceThicknessMap");
	pushIfPresent("thicknessMapPath", "thicknessMap");
	pushIfPresent("diffuseTransmissionMapPath", "diffuseTransmissionMap");
	pushIfPresent("diffuseTransmissionColorMapPath", "diffuseTransmissionColorMap");


	// Also add common legacy ADS keys (in case materials were saved using legacy names)
	pushIfPresent("albedoMap", "albedoMap");
	pushIfPresent("diffuse", "texture_diffuse");
	pushIfPresent("specular", "texture_specular");
	pushIfPresent("emissive", "texture_emissive");
	pushIfPresent("normal", "texture_normal");
	pushIfPresent("opacity", "texture_opacity");

	// If we added anything, rebuild mesh flags and buffers
	if (!_textures.empty())
	{
		// Recompute _hasXXX flags and buffers
		setupMesh();

		// Ensure the next render refresh uses new textures/uniforms
		markTexturesDirty();
		markUniformsDirty();
	}
}


GLuint AssImpMesh::createGLTextureFromFile(const QString& fullPath, bool& outHasAlpha)
{
	outHasAlpha = false;
	if (fullPath.isEmpty()) return 0;
	if (!QFileInfo::exists(fullPath))
	{
		qWarning() << "createGLTextureFromFile: file not found:" << fullPath;
		return 0;
	}

	QImage img;
	if (!img.load(fullPath))
	{
		qWarning() << "createGLTextureFromFile: QImage failed to load:" << fullPath;
		return 0;
	}

	// Detect alpha before any conversion
	outHasAlpha = img.hasAlphaChannel();

	// Convert to a known format and flip vertically to match GL origin (bottom-left)
	QImage glimg = img.convertToFormat(QImage::Format_ARGB32);
	glimg = glimg.mirrored(false, true); // horizontal=false, vertical=true

	// Ensure GL context is present (caller must be on GL thread)
	if (!QOpenGLContext::currentContext())
	{
		qWarning() << "createGLTextureFromFile: no GL context; cannot create texture now for" << fullPath;
		return 0;
	}

	GLuint tex = 0;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);

	// Defensive: ensure unpack alignment won't cause row padding problems
	GLint prevAlign = 0;
	glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevAlign);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	// Upload: QImage::Format_ARGB32 corresponds to BGRA ordering on many platforms.
	glTexImage2D(GL_TEXTURE_2D,
		0,
		GL_RGBA,
		glimg.width(),
		glimg.height(),
		0,
		GL_BGRA,
		GL_UNSIGNED_BYTE,
		glimg.bits());

	// Restore previous alignment
	glPixelStorei(GL_UNPACK_ALIGNMENT, prevAlign);

	glGenerateMipmap(GL_TEXTURE_2D);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	// Optional: set wrap modes if needed (repeat/clamp)
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

	glBindTexture(GL_TEXTURE_2D, 0);
	return tex;
}


vector<Vertex> AssImpMesh::vertices() const
{
    return _vertices;
}

vector<unsigned int> AssImpMesh::indices() const
{
    return _indices;
}

vector<GLMaterial::Texture> AssImpMesh::textures() const
{
    return _textures;
}

void AssImpMesh::getMeshData(std::vector<Vertex>& vertices,
	std::vector<unsigned int>& indices) const
{
	vertices = _vertices;
	indices = _indices;
}

// Set new mesh data and upload to GPU (no optimization)
void AssImpMesh::setMeshData(const std::vector<Vertex>& vertices,
	const std::vector<unsigned int>& indices)
{
	_vertices = vertices;
	_indices = indices;

	// Re-upload to GPU (no optimization)
	setupMesh();

	// Setup transformation again (in case bounds changed)
	setupTransformation();
}


// --- Serialization ---
void AssImpMesh::serialize(QDataStream& out) const
{
	// Write mesh name
	out << _name;

	// Write vertices
	out << static_cast<quint32>(_vertices.size());
	for (const Vertex& v : _vertices) {
		out << v.Position.x << v.Position.y << v.Position.z;
		out << v.Normal.x << v.Normal.y << v.Normal.z;
		out << v.TexCoords[0].x << v.TexCoords[0].y;
		out << v.Tangent.x << v.Tangent.y << v.Tangent.z;
		out << v.Bitangent.x << v.Bitangent.y << v.Bitangent.z;
	}

	// Write indices
	out << static_cast<quint32>(_indices.size());
	for (unsigned int idx : _indices) {
		out << static_cast<quint32>(idx);
	}

	// Write textures (paths and types as QString)
	out << static_cast<quint32>(_textures.size());
	for (const GLMaterial::Texture& t : _textures)
	{
		// convert std::string (Assimp) to QString explicitly using UTF-8
		QString qtype = QString::fromUtf8(t.type.c_str());
		QString qpath = QString::fromUtf8(t.path);
		out << qtype;
		out << qpath;
	}


	// Write material (assuming GLMaterial is serializable, otherwise write its fields)
	_material.serialize(out);

	// Write the transform matrix
	out << _transformation;
}

// --- Deserialization ---
void AssImpMesh::deserialize(QDataStream& in)
{
	// Read mesh name
	in >> _name;

	// Read vertices
	quint32 vCount;
	in >> vCount;
	_vertices.clear();
	_vertices.reserve(vCount);
	for (quint32 i = 0; i < vCount; ++i) {
		Vertex v;
		in >> v.Position.x >> v.Position.y >> v.Position.z;
		in >> v.Normal.x >> v.Normal.y >> v.Normal.z;
		in >> v.TexCoords[0].x >> v.TexCoords[0].y;
		in >> v.Tangent.x >> v.Tangent.y >> v.Tangent.z;
		in >> v.Bitangent.x >> v.Bitangent.y >> v.Bitangent.z;
		_vertices.push_back(v);
	}

	// Read indices
	quint32 iCount;
	in >> iCount;
	_indices.clear();
	_indices.reserve(iCount);
	for (quint32 i = 0; i < iCount; ++i) {
		quint32 idx;
		in >> idx;
		_indices.push_back(idx);
	}

	// Read textures
	quint32 tCount;
	in >> tCount;
	_textures.clear();
	_textures.reserve(tCount);
	for (quint32 i = 0; i < tCount; ++i)
	{
		QString typeQ, pathQ;
		in >> typeQ >> pathQ;
		if (in.status() != QDataStream::Ok)
		{
			qWarning() << "AssImpMesh::deserialize: failed reading texture entry" << i << "pos" << (in.device() ? in.device()->pos() : -1);
			return;
		}
		GLMaterial::Texture t;
		t.type = typeQ.toStdString();
		t.path = pathQ.toStdString();
		t.id = 0; // must be reloaded via TextureManager later
		_textures.push_back(t);
	}

	// Read material
	_material.deserialize(in);

	// --- Ensure deserialized texture paths get actual GL texture IDs and keep material in sync ---
	// Helper lambda: convert our Texture.type -> update GLMaterial
	auto updateMaterialFromTexture = [this](const GLMaterial::Texture& tex) {
		QString qpath = QString::fromUtf8(tex.path).trimmed();
		if (qpath.isEmpty()) return;

		const std::string ttype = tex.type;
		if (ttype == "albedoMap" || ttype == "texture_diffuse")
		{
			_material.setAlbedoTextureId(tex.id);
			_material.setAlbedoMap(qpath);
		}
		else if (ttype == "normalMap" || ttype == "texture_normal")
		{
			_material.setNormalTextureId(tex.id);
			_material.setNormalMap(qpath);
		}
		else if (ttype == "metallicMap" || ttype == "texture_specular")
		{
			_material.setMetallicTextureId(tex.id);
			_material.setMetallicMap(qpath);
		}
		else if (ttype == "roughnessMap")
		{
			_material.setRoughnessTextureId(tex.id);
			_material.setRoughnessMap(qpath);
		}
		else if (ttype == "emissiveMap" || ttype == "texture_emissive")
		{
			_material.setEmissiveTextureId(tex.id);
			_material.setEmissiveMap(qpath);
		}
		else if (ttype == "heightMap" || ttype == "texture_height")
		{
			_material.setHeightTextureId(tex.id);
			_material.setHeightMap(qpath);
		}
		else if (ttype == "opacityMap" || ttype == "texture_opacity")
		{
			_material.setOpacityTextureId(tex.id);
			_material.setOpacityMap(qpath);
		}
		else if (ttype == "aoMap")
		{
			_material.setOcclusionTextureId(tex.id);
			_material.setAOMap(qpath);
		}
		else if (ttype == "transmissionMap")
		{
			_material.setTransmissionTextureId(tex.id);
			_material.setTransmissionMap(qpath);
		}
		else if (ttype == "iorMap")
		{
			_material.setIORTextureId(tex.id);
			_material.setIORMap(qpath);
		}
		else if (ttype == "sheenColorMap")
		{
			_material.setSheenColorTextureId(tex.id);
			_material.setSheenColorMap(qpath);
		}
		else if (ttype == "sheenRoughnessMap")
		{
			_material.setSheenRoughnessTextureId(tex.id);
			_material.setSheenRoughnessMap(qpath);
		}
		else if (ttype == "clearcoatColorMap")
		{
			_material.setClearcoatColorTextureId(tex.id);
			_material.setClearcoatColorMap(qpath);
		}
		else if (ttype == "clearcoatRoughnessMap")
		{
			_material.setClearcoatRoughnessTextureId(tex.id);
			_material.setClearcoatRoughnessMap(qpath);
		}
		else if (ttype == "clearcoatNormalMap")
		{
			_material.setClearcoatNormalTextureId(tex.id);
			_material.setClearcoatNormalMap(qpath);
		}
		// add other mappings if GLMaterial provides the setters
		};

	// Iterate existing _textures vector:
	for (size_t i = 0; i < _textures.size(); ++i)
	{
		GLMaterial::Texture& t = _textures[i];
		QString path = QString::fromUtf8(t.path).trimmed();

		// If there is a path but id==0, create GL texture now
		if (!path.isEmpty() && t.id == 0)
		{
			bool hasAlpha = false;
			GLuint newId = createGLTextureFromFile(path, hasAlpha);
			if (newId != 0)
			{
				t.id = static_cast<unsigned int>(newId);
				t.hasAlpha = hasAlpha;

				// register/replace in the mesh's internal bindings list
				replaceOrAppendTexture(t.type, newId, hasAlpha);

				// also update GLMaterial so both mesh and material are in agreement
				updateMaterialFromTexture(t);
			}
			else
			{
				qWarning() << "AssImpMesh::deserialize: failed to create GL texture for" << path;
			}
		}
		else if (!path.isEmpty())
		{
			// path present and maybe id was non-zero already (unlikely in serialized stream),
			// make sure material is at least aware of the mapping
			updateMaterialFromTexture(t);
		}
	}

	// If we didn't find any _textures entries but material has paths, try the material-based sync
	bool meshHasAnyPath = false;
	for (const GLMaterial::Texture& tt : _textures)
	{
		if (!QString::fromUtf8(tt.path).isEmpty()) { meshHasAnyPath = true; break; }
	}
	if (!meshHasAnyPath)
	{
		// fall back to creating textures from material (this code already exists in syncTexturesFromMaterialIfNeeded)
		syncTexturesFromMaterialIfNeeded();
	}

	// finally, recompute flags and rebuild if we added textures
	if (!_textures.empty())
	{
		setupMesh();
		markTexturesDirty();
		markUniformsDirty();
	}


	// Read the transformation matrix
	in >> _transformation;

	// Re-setup OpenGL buffers
	setupMesh();

	// Set the transformation matrix
	setupTransformation();
}

void AssImpMesh::setAlbedoPBRMap(unsigned int albedoMap)
{
	//glDeleteTextures(1, &_albedoPBRMap);
	_albedoPBRMap = albedoMap;
	replaceOrAppendTexture("albedoMap", albedoMap, false);
	markTexturesDirty();
	markUniformsDirty();
}

void AssImpMesh::setMetallicPBRMap(unsigned int metallicMap)
{
	//glDeleteTextures(1, &_metallicPBRMap);
	_metallicPBRMap = metallicMap;
	replaceOrAppendTexture("metallicMap", metallicMap, false);
	markTexturesDirty();
	markUniformsDirty();
}

void AssImpMesh::setEmissivePBRMap(unsigned int emissiveMap)
{
	//glDeleteTextures(1, &_emissivePBRMap);
	_emissivePBRMap = emissiveMap;
	replaceOrAppendTexture("emissiveMap", emissiveMap, false);
	markTexturesDirty();
	markUniformsDirty();
}

void AssImpMesh::setRoughnessPBRMap(unsigned int roughnessMap)
{
	//glDeleteTextures(1, &_roughnessPBRMap);
	_roughnessPBRMap = roughnessMap;
	replaceOrAppendTexture("roughnessMap", roughnessMap, false);
	markTexturesDirty();
	markUniformsDirty();
}

void AssImpMesh::setNormalPBRMap(unsigned int normalMap)
{
	//glDeleteTextures(1, &_normalPBRMap);
	_normalPBRMap = normalMap;
	replaceOrAppendTexture("normalMap", normalMap, false);
	markTexturesDirty();
	markUniformsDirty();
}

void AssImpMesh::setAOPBRMap(unsigned int aoMap)
{
	//glDeleteTextures(1, &_aoPBRMap);
	_aoPBRMap = aoMap;
	replaceOrAppendTexture("aoMap", aoMap, false);
	markTexturesDirty();
	markUniformsDirty();
}

void AssImpMesh::setHeightPBRMap(unsigned int heightMap)
{
	//glDeleteTextures(1, &_heightPBRMap);
	_heightPBRMap = heightMap;
	replaceOrAppendTexture("heightMap", heightMap, false);
	markTexturesDirty();
	markUniformsDirty();
}

void AssImpMesh::setOpacityPBRMap(unsigned int opacityMap)
{
	//glDeleteTextures(1, &_opacityPBRMap);
	_opacityPBRMap = opacityMap;
	_material.setBlendMode(GLMaterial::BlendMode::Alpha);
	replaceOrAppendTexture("opacityMap", opacityMap, true);
	markTexturesDirty();
	markUniformsDirty();
}

void AssImpMesh::setIORPBRMap(unsigned int iorMap)
{
	//glDeleteTextures(1, &_IORPBRMap);
	_IORPBRMap = iorMap;
	replaceOrAppendTexture("iorMap", iorMap, false);
	markTexturesDirty();
	markUniformsDirty();
}

void AssImpMesh::setClearcoatPBRMap(unsigned int clearcoatColorMap)
{
	//glDeleteTextures(1, &_clearcoatPBRMap);
	_clearcoatPBRMap = clearcoatColorMap;
	replaceOrAppendTexture("clearcoatColorMap", clearcoatColorMap, false);
	markTexturesDirty();
	markUniformsDirty();
}

void AssImpMesh::setClearcoatRoughnessPBRMap(unsigned int clearcoatRoughnessMap)
{
	//glDeleteTextures(1, &_clearcoatRoughnessPBRMap);
	_clearcoatRoughnessPBRMap = clearcoatRoughnessMap;
	replaceOrAppendTexture("clearcoatRoughnessMap", clearcoatRoughnessMap, false);
	markTexturesDirty();
	markUniformsDirty();
}

void AssImpMesh::setClearcoatNormalPBRMap(unsigned int clearcoatNormalMap)
{
	//glDeleteTextures(1, &_clearcoatNormalPBRMap);
	_clearcoatNormalPBRMap = clearcoatNormalMap;
	replaceOrAppendTexture("clearcoatNormalMap", clearcoatNormalMap, false);
	markTexturesDirty();
	markUniformsDirty();
}

void AssImpMesh::setSheenColorPBRMap(unsigned int sheenMap)
{
	//glDeleteTextures(1, &_sheenColorPBRMap);
	_sheenColorPBRMap = sheenMap;
	replaceOrAppendTexture("sheenColorMap", sheenMap, false);
	markTexturesDirty();
	markUniformsDirty();
}

void AssImpMesh::setSheenRoughnessPBRMap(unsigned int sheenRoughnessMap)
{
	//glDeleteTextures(1, &_sheenRoughnessPBRMap);
	_sheenRoughnessPBRMap = sheenRoughnessMap;
	replaceOrAppendTexture("sheenRoughnessMap", sheenRoughnessMap, false);
	markTexturesDirty();
	markUniformsDirty();
}

void AssImpMesh::setTransmissionPBRMap(unsigned int transmissionMap)
{
	//glDeleteTextures(1, &_transmissionPBRMap);
	_transmissionPBRMap = transmissionMap;
	replaceOrAppendTexture("transmissionMap", transmissionMap, false);
	markTexturesDirty();
	markUniformsDirty();
}

void AssImpMesh::setDiffuseADSMap(unsigned int diffuseTex)
{
	//glDeleteTextures(1, &_diffuseADSMap);
	_diffuseADSMap = diffuseTex;
	GLMaterial::Texture t;
	t.id = diffuseTex;
	t.type = "texture_diffuse";
	t.path = ""; // No path for OpenGL texture handles
	_textures.push_back(t);
	_hasDiffuseADSMap = true;
	markTexturesDirty();
	markUniformsDirty();
}

void AssImpMesh::setSpecularADSMap(unsigned int specularTex)
{
	//glDeleteTextures(1, &_specularADSMap);
	_specularADSMap = specularTex;
	GLMaterial::Texture t;
	t.id = specularTex;
	t.type = "texture_specular";
	t.path = ""; // No path for OpenGL texture handles
	_textures.push_back(t);
	_hasSpecularADSMap = true;
	markTexturesDirty();
	markUniformsDirty();
}

void AssImpMesh::setEmissiveADSMap(unsigned int emissiveTex)
{
	//glDeleteTextures(1, &_emissiveADSMap);
	_emissiveADSMap = emissiveTex;
	GLMaterial::Texture t;
	t.id = emissiveTex;
	t.type = "texture_emissive";
	t.path = ""; // No path for OpenGL texture handles
	_textures.push_back(t);
	_hasEmissiveADSMap = true;
	markTexturesDirty();
	markUniformsDirty();
}

void AssImpMesh::setNormalADSMap(unsigned int normalTex)
{
	//glDeleteTextures(1, &_normalADSMap);
	_normalADSMap = normalTex;
	GLMaterial::Texture t;
	t.id = normalTex;
	t.type = "texture_normal";
	t.path = ""; // No path for OpenGL texture handles
	_textures.push_back(t);
	_hasNormalADSMap = true;
	markTexturesDirty();
	markUniformsDirty();
}

void AssImpMesh::setHeightADSMap(unsigned int heightTex)
{
	//glDeleteTextures(1, &_heightADSMap);
	_heightADSMap = heightTex;
	GLMaterial::Texture t;
	t.id = heightTex;
	t.type = "texture_height";
	t.path = ""; // No path for OpenGL texture handles
	_textures.push_back(t);
	_hasHeightADSMap = true;
	markTexturesDirty();
	markUniformsDirty();
}

void AssImpMesh::setOpacityADSMap(unsigned int opacityTex)
{
	//glDeleteTextures(1, &_opacityADSMap);
	_opacityADSMap = opacityTex;
	GLMaterial::Texture t;
	t.id = opacityTex;
	t.type = "texture_opacity";
	t.path = ""; // No path for OpenGL texture handles
	_textures.push_back(t);
	_hasOpacityADSMap = true;
	_material.setBlendMode(GLMaterial::BlendMode::Alpha);
	markTexturesDirty();
	markUniformsDirty();
}

void AssImpMesh::setTextureMaps(const GLMaterial& material)
{
	_textures.clear();
	_hasAlbedoPBRMap = false;
	_hasMetallicPBRMap = false;
	_hasEmissivePBRMap = false;
	_hasRoughnessPBRMap = false;
	_hasNormalPBRMap = false;
	_hasAOPBRMap = false;
	_hasHeightPBRMap = false;
	_hasOpacityPBRMap = false;
	_hasTransmissionPBRMap = false;
	_hasIORPBRMap = false;
	_hasSheenColorPBRMap = false;
	_hasSheenRoughnessPBRMap = false;
	_hasClearcoatPBRMap = false;
	_hasClearcoatRoughnessPBRMap = false;
	_hasClearcoatNormalPBRMap = false;

	if (material.hasAlbedoMap())
	{
		_hasAlbedoPBRMap = true;
		setAlbedoPBRMap(material.albedoTextureId());
	}
	if (material.hasMetallicMap())
	{
		_hasMetallicPBRMap = true;
		setMetallicPBRMap(material.metallicTextureId());
	}
	if (material.hasEmissiveMap())
	{
		_hasEmissivePBRMap = true;
		setEmissivePBRMap(material.emissiveTextureId());
	}
	if (material.hasRoughnessMap())
	{
		_hasRoughnessPBRMap = true;
		setRoughnessPBRMap(material.roughnessTextureId());
	}
	if (material.hasNormalMap())
	{
		_hasNormalPBRMap = true;
		setNormalPBRMap(material.normalTextureId());
	}
	if (material.hasAOMap())
	{
		_hasAOPBRMap = true;
		setAOPBRMap(material.occlusionTextureId());
	}
	if (material.hasHeightMap())
	{
		_hasHeightPBRMap = true;
		setHeightPBRMap(material.heightTextureId());
	}
	if (material.hasOpacityMap())
	{
		_hasOpacityPBRMap = true;
		setOpacityPBRMap(material.opacityTextureId());
	}
	if (material.hasTransmissionMap())
	{
		_hasTransmissionPBRMap = true;
		setTransmissionPBRMap(material.transmissionTextureId());
	}
	if (material.hasIORMap())
	{
		_hasIORPBRMap = true;
		setIORPBRMap(material.iorTextureId());
	}
	if (material.hasSheenColorMap())
	{
		_hasSheenColorPBRMap = true;
		setSheenColorPBRMap(material.sheenColorTextureId());
	}
	if (material.hasSheenRoughnessMap())
	{
		_hasSheenRoughnessPBRMap = true;
		setSheenRoughnessPBRMap(material.sheenRoughnessTextureId());
	}
	if (material.hasClearcoatColorMap())
	{
		_hasClearcoatPBRMap = true;
		setClearcoatPBRMap(material.clearcoatColorTextureId());
	}
	if (material.hasClearcoatRoughnessMap())
	{
		_hasClearcoatRoughnessPBRMap = true;
		setClearcoatRoughnessPBRMap(material.clearcoatRoughnessTextureId());
	}
	if (material.hasClearcoatNormalMap())
	{
		_hasClearcoatNormalPBRMap = true;
		setClearcoatNormalPBRMap(material.clearcoatNormalTextureId());
	}
	if (material.isOpacityMapInverted())
	{
		_opacityPBRMapInverted = true;
		_opacityADSMapInverted = true;
	}
	else
	{
		_opacityPBRMapInverted = false;
		_opacityADSMapInverted = false;
	}
	_material = material;
}

void AssImpMesh::replaceOrAppendTexture(const std::string& type, GLuint id, bool hasAlpha)
{
	for (auto& t : _textures)
	{
		if (t.type == type)
		{
			t.id = id;
			t.hasAlpha = hasAlpha;
			return;
		}
	}
	GLMaterial::Texture t; t.id = id; t.type = type; t.hasAlpha = hasAlpha;
	_textures.push_back(t);
}


// Cleanup method
void AssImpMesh::releaseCurrentShader()
{
	if (_currentBoundShader)
	{
		QOpenGLContext* ctx = QOpenGLContext::currentContext();
		if (ctx && ctx->isValid() && ctx->thread() == QThread::currentThread())
		{
			_currentBoundShader->release();
		}
	}
	_currentBoundShader = nullptr;
}

