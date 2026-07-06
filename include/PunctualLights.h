#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <vector>
#include <QOpenGLFunctions_4_5_Core>
#include <QMetaType>

enum class LightType
{
    Directional = 0,
    Point = 1,
    Spot = 2
};

struct GPULight
{
    glm::vec3 direction;      // 12 bytes
    float range;              // 4 bytes

    glm::vec3 color;          // 12 bytes
    float intensity;          // 4 bytes

    glm::vec3 position;       // 12 bytes
    float innerConeCos;       // 4 bytes

    float outerConeCos;       // 4 bytes
    int type;                 // 4 bytes

    glm::vec2 padding;        // 8 bytes (std140 alignment)
};

Q_DECLARE_METATYPE(GPULight)
Q_DECLARE_METATYPE(std::vector<GPULight>)

class PunctualLights : public QOpenGLFunctions_4_5_Core
{
public:
    static const int MAX_LIGHTS = 16;
    static const int LIGHT_UBO_BINDING = 3;

    PunctualLights();
    ~PunctualLights();

    // Set lights from parsed glTF (with transforms already applied)
    void setLights(const std::vector<GPULight>& lights);

    // Create a single fallback light (for models without KHR_lights_punctual)
    void createFallbackLight(const glm::vec3& position);

    // Bind the light UBO to a shader program
    void bind(GLuint shaderProgram, const char* blockName = "LightBlock");

    // Get light count for shader compilation or debugging
    int getLightCount() const { return lightCount; }

    // Current world-space light list (same data already bound to the raster
    // UBO via setLights()) - lets other consumers (RtSceneBuilder, for the
    // path-traced display mode) read the exact same lights the raster pass
    // uses, guaranteeing the two stay in sync without re-deriving anything.
    const std::vector<GPULight>& getLights() const { return lights; }

    // Clean up GPU resources
    void cleanup();

private:
    std::vector<GPULight> lights;
    GLuint uboHandle;
    int lightCount;

    void createUBO();
    void uploadData();
};
