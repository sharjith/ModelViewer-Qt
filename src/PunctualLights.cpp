#include "PunctualLights.h"
#include <QDebug>

PunctualLights::PunctualLights() : uboHandle(0), lightCount(0)
{
    initializeOpenGLFunctions();
}

PunctualLights::~PunctualLights()
{
    cleanup();
}

void PunctualLights::setLights(const std::vector<GPULight>& lightsData)
{
    lights = lightsData;
    lightCount = static_cast<int>(lights.size());

    if (lightCount > MAX_LIGHTS)
    {
        lightCount = MAX_LIGHTS;
        lights.resize(lightCount);
        qWarning() << "PunctualLights::setLights: Clamping light count to" << MAX_LIGHTS;
    }

    if (lightCount > 0)
    {
        uploadData();
    }

    qDebug() << "PunctualLights::setLights: Set" << lightCount << "lights with transforms applied";
}

void PunctualLights::createFallbackLight(const glm::vec3& position, float intensity)
{
    lights.clear();
    lightCount = 0;

    GPULight fallback = {};
    fallback.type = static_cast<int>(LightType::Point);
    fallback.position = position;
    fallback.color = glm::vec3(1.0f);
    fallback.intensity = intensity;
    fallback.range = 0.0f;  // Infinite range
    fallback.direction = glm::vec3(0.0f, 0.0f, -1.0f);
    fallback.innerConeCos = std::cos(0.0f);
    fallback.outerConeCos = std::cos(glm::pi<float>() / 4.0f);
    fallback.padding = glm::vec2(0.0f);

    lights.push_back(fallback);
    lightCount = 1;

    uploadData();

    qDebug() << "PunctualLights::createFallbackLight: Created fallback point light at"
        << position.x << "," << position.y << "," << position.z << "intensity=" << intensity;
}

void PunctualLights::bind(GLuint shaderProgram, const char* blockName)
{
    if (lightCount == 0 || uboHandle == 0)
    {
        return;
    }

    GLuint blockIndex = glGetUniformBlockIndex(shaderProgram, blockName);
    if (blockIndex == GL_INVALID_INDEX)
    {
        qWarning() << "PunctualLights::bind: Could not find uniform block" << blockName;
        return;
    }

    glUniformBlockBinding(shaderProgram, blockIndex, LIGHT_UBO_BINDING);
    glBindBufferBase(GL_UNIFORM_BUFFER, LIGHT_UBO_BINDING, uboHandle);
}

void PunctualLights::cleanup()
{
    if (uboHandle != 0)
    {
        glDeleteBuffers(1, &uboHandle);
        uboHandle = 0;
    }
    lights.clear();
    lightCount = 0;
}

void PunctualLights::createUBO()
{
    if (uboHandle == 0)
    {
        glGenBuffers(1, &uboHandle);
        glBindBuffer(GL_UNIFORM_BUFFER, uboHandle);
        glBufferData(GL_UNIFORM_BUFFER, MAX_LIGHTS * sizeof(GPULight), nullptr, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
    }
}

void PunctualLights::uploadData()
{
    if (lightCount == 0)
    {
        return;
    }

    createUBO();

    size_t dataSize = lightCount * sizeof(GPULight);

    glBindBuffer(GL_UNIFORM_BUFFER, uboHandle);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, dataSize, lights.data());
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    qDebug() << "PunctualLights::uploadData: Uploaded" << lightCount << "light(s) to GPU";
}
