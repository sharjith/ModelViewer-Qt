#include "AssImpModelLoader.h"

using namespace std;

bool AssImpModelProgressHandler::Update(float percentage)
{
    emit fileReadProcessed(percentage);
    return true;
}


/*  Functions   */
// Constructor, expects a filepath to a 3D model.
AssImpModelLoader::AssImpModelLoader(QOpenGLShaderProgram* prog) : QObject(), _prog(prog)
{
    initializeOpenGLFunctions();
    _progHandler = new AssImpModelProgressHandler();
    _importer.SetProgressHandler(_progHandler);
    connect(_progHandler, SIGNAL(fileReadProcessed(float)), this, SLOT(processFileReadProgress(float)));
}

AssImpModelLoader::~AssImpModelLoader()
{
    disconnect(_progHandler, SIGNAL(fileReadProcessed(float)), this, SLOT(processFileReadProgress(float)));
    //delete _progHandler; // causes crash
    _progHandler = nullptr;
}

void AssImpModelLoader::processFileReadProgress(float percentage)
{
    emit fileReadProcessed(percentage);
}

vector<AssImpMesh*> AssImpModelLoader::getMeshes() const
{
    return _meshes;
}

/*  Functions   */
// Loads a model with supported ASSIMP extensions from file and stores the resulting meshes in the meshes vector.
void AssImpModelLoader::loadModel(string path)
{
    _path = std::string(path);
    _meshes.clear();
    _loadedTextures.clear();
    // Read file via ASSIMP
    _importer.SetPropertyFloat("PP_GSN_MAX_SMOOTHING_ANGLE", 90);
    const aiScene* scene = _importer.ReadFile(path, aiProcess_CalcTangentSpace | aiProcess_JoinIdenticalVertices |
                                              aiProcess_Triangulate | aiProcess_GenUVCoords |
                                              aiProcess_SortByPType | aiProcess_JoinIdenticalVertices |
                                              aiProcess_FindInvalidData | aiProcess_GenSmoothNormals | aiProcess_ImproveCacheLocality);

    // Check for errors
    if (!scene || scene->mFlags == AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) // if is Not Zero
    {
        cout << "ERROR::ASSIMP:: " << _importer.GetErrorString() << endl;
        return;
    }
    // Retrieve the directory path of the filepath
    this->directory = path.substr(0, path.find_last_of('/'));

    // Process ASSIMP's root node recursively
    this->processNode(0, scene->mRootNode, scene);
}

// Processes a node in a recursive fashion. Processes each individual mesh located at the node and repeats this process on its children nodes (if any).
void AssImpModelLoader::processNode(int nodeNum, aiNode* node, const aiScene* scene)
{
    // Process each mesh located at the current node
    for (unsigned int i = 0; i < node->mNumMeshes; i++)
    {
        // The node object only contains indices to index the actual objects in the scene.
        // The scene contains all the data, node is just to keep stuff organized (like relations between nodes).
        aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];

        this->_meshes.push_back(this->processMesh(mesh, scene));
    }

    // After we've processed all of the meshes (if any) we then recursively process each of the children nodes
    for (unsigned int i = 0; i < node->mNumChildren; i++)
    {
        this->processNode(++nodeNum, node->mChildren[i], scene);
        emit nodeProcessed(nodeNum, node->mNumChildren);
    }
}

AssImpMesh* AssImpModelLoader::processMesh(aiMesh* mesh, const aiScene* scene)
{
    // Data to fill
    vector<Vertex> vertices;
    vector<unsigned int> indices;
    vector<Texture> textures;

    // Walk through each of the mesh's vertices
    int step = 0;
    unsigned int nbVertices = mesh->mNumVertices;
    for (unsigned int i = 0; i < nbVertices; i++)
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
        if (mesh->mTextureCoords[0]) // Does the mesh contain texture coordinates?
        {
            glm::vec2 vec;
            // A vertex can contain up to 8 different texture coordinates. We thus make the assumption that we won't
            // use models where a vertex can have multiple texture coordinates so we always take the first set (0).
            vec.x = mesh->mTextureCoords[0][i].x;
            vec.y = mesh->mTextureCoords[0][i].y;
            vertex.TexCoords = vec;

            // tangent
            if (mesh->mTangents)
            {
                vector.x = mesh->mTangents[i].x;
                vector.y = mesh->mTangents[i].y;
                vector.z = mesh->mTangents[i].z;
                vertex.Tangent = vector;
            }
            // bitangent
            if (mesh->mBitangents)
            {
                vector.x = mesh->mBitangents[i].x;
                vector.y = mesh->mBitangents[i].y;
                vector.z = mesh->mBitangents[i].z;
                vertex.Bitangent = vector;
            }
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
        vertices.push_back(vertex);

        if(i % 100000 == 0)
        {
            emit verticesProcessed(static_cast<float>(i)/nbVertices * 100.0f);
        }
    }

    // Now wak through each of the mesh's faces (a face is a mesh its triangle) and retrieve the corresponding vertex indices.
    for (unsigned int i = 0; i < mesh->mNumFaces; i++)
    {
        aiFace face = mesh->mFaces[i];
        // Retrieve all indices of the face and store them in the indices vector
        for (unsigned int j = 0; j < face.mNumIndices; j++)
        {
            indices.push_back(face.mIndices[j]);
        }
    }

    // Process materials
    GLMaterial mat;
    if (mesh->mMaterialIndex != 0)
    {
        aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
        // We assume a convention for sampler names in the shaders. Each diffuse texture should be named
        // as 'texture_diffuseN' where N is a sequential number ranging from 1 to MAX_SAMPLER_NUMBER.
        // Same applies to other texture as the following list summarizes:
        // Diffuse: texture_diffuseN
        // Specular: texture_specularN
        // Normal: texture_normalN

        // 1. diffuse maps
        vector<Texture> diffuseMaps = loadMaterialTextures(material, aiTextureType_DIFFUSE, "texture_diffuse");
        textures.insert(textures.end(), diffuseMaps.begin(), diffuseMaps.end());
        // 2. specular maps
        vector<Texture> specularMaps = loadMaterialTextures(material, aiTextureType_SPECULAR, "texture_specular");
        textures.insert(textures.end(), specularMaps.begin(), specularMaps.end());
        // 2. emissive maps
        vector<Texture> emissiveMaps = loadMaterialTextures(material, aiTextureType_EMISSIVE, "texture_emissive");
        textures.insert(textures.end(), emissiveMaps.begin(), emissiveMaps.end());
        // 4. normal maps
        std::vector<Texture> normalMaps = loadMaterialTextures(material, aiTextureType_HEIGHT, "texture_normal");
        textures.insert(textures.end(), normalMaps.begin(), normalMaps.end());
        // 5. height maps
        std::vector<Texture> heightMaps = loadMaterialTextures(material, aiTextureType_DISPLACEMENT, "texture_height");
        textures.insert(textures.end(), heightMaps.begin(), heightMaps.end());
        // 5. opacity maps
        std::vector<Texture> opacityMaps = loadMaterialTextures(material, aiTextureType_OPACITY, "texture_opacity");
        textures.insert(textures.end(), opacityMaps.begin(), opacityMaps.end());

        aiColor3D color(0.f, 0.f, 0.f);
        float opacity = 1.0f;
        material->Get(AI_MATKEY_OPACITY, opacity);
        if (AI_SUCCESS == material->Get(AI_MATKEY_COLOR_AMBIENT, color))
        {
            mat.setAmbient(QVector3D(color.r, color.g, color.b));
        }
        if (AI_SUCCESS == material->Get(AI_MATKEY_COLOR_DIFFUSE, color))
        {
            mat.setDiffuse(QVector3D(color.r, color.g, color.b));
        }
        if (AI_SUCCESS == material->Get(AI_MATKEY_COLOR_SPECULAR, color))
        {
            mat.setSpecular(QVector3D(color.r, color.g, color.b));
        }
        if (AI_SUCCESS == material->Get(AI_MATKEY_COLOR_EMISSIVE, color))
        {
            mat.setEmissive(QVector3D(color.r, color.g, color.b));
        }
        if (AI_SUCCESS == material->Get(AI_MATKEY_OPACITY, opacity))
        {
            mat.setOpacity(opacity);
        }
    }

    // Return a mesh object created from the extracted mesh data
    return new AssImpMesh(_prog, QFileInfo(QString(_path.data())).baseName(), vertices, indices, textures, mat);
}

// Checks all material textures of a given type and loads the textures if they're not loaded yet.
// The required info is returned as a Texture struct.
vector<Texture> AssImpModelLoader::loadMaterialTextures(aiMaterial* mat, aiTextureType type, string typeName)
{
    vector<Texture> textures;

    for (unsigned int i = 0; i < mat->GetTextureCount(type); i++)
    {
        aiString str;
        mat->GetTexture(type, i, &str);

        // Check if texture was loaded before and if so, continue to next iteration: skip loading a new texture
        GLboolean skip = false;

        for (unsigned int j = 0; j < _loadedTextures.size(); j++)
        {
            if (_loadedTextures[j].path == str)
            {
                textures.push_back(_loadedTextures[j]);
                skip = true; // A texture with the same filepath has already been loaded, continue to next one. (optimization)
                break;
            }
        }

        if (!skip)
        {
            // If texture hasn't been loaded already, load it
            Texture texture;
            texture.id = textureFromFile(str.C_Str(), this->directory);
            texture.type = typeName;
            texture.path = str;
            textures.push_back(texture);

            this->_loadedTextures.push_back(texture);  // Store it as texture loaded for entire model, to ensure we won't unnecesery load duplicate textures.
        }
    }

    return textures;
}

unsigned int AssImpModelLoader::textureFromFile(const char* path, string directory)
{
    //Generate texture ID and load texture data
    string filename = string(path);
    filename = directory + '/' + filename;
    unsigned int textureID;
    glGenTextures(1, &textureID);

    QImage texImage;

    if (!texImage.load(QString(filename.c_str())))
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
    glBindTexture(GL_TEXTURE_2D, textureID);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texImage.width(), texImage.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, texImage.bits());
    glGenerateMipmap(GL_TEXTURE_2D);

    // Parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

    return textureID;
}

