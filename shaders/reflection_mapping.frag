#version 450 core
in vec3 v_position;
in vec3 v_normal;
in vec2 v_texCoord2d;
in vec3 v_reflectionNormal;


uniform float alpha;
uniform bool texEnabled;
uniform sampler2D texUnit;
uniform samplerCube envMap;
uniform sampler2D shadowMap;
uniform sampler2D reflectionMap;
uniform bool envMapEnabled;
uniform bool shadowsEnabled;
uniform bool reflectionsEnabled;
uniform vec3 cameraPos;
uniform mat4 viewMatrix;
uniform bool sectionActive;
uniform int displayMode;
uniform bool selected;

struct LineInfo
{
  float Width;
  vec4 Color;
};

uniform LineInfo Line;


struct LightSource
{
    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
    vec3 position;
};
uniform LightSource lightSource;

struct LightModel
{
    vec3 ambient;
};
uniform LightModel lightModel;

struct Material {
    vec3  emission;
    vec3  ambient;
    vec3  diffuse;
    vec3  specular;
    float shininess;
};
uniform Material material;

layout( location = 0 ) out vec4 fragColor;

vec3 shadeBlinnPhong(LightSource source, LightModel model, Material mat, vec3 position, vec3 normal)
{
    vec3 halfVector = normalize(source.position + vec3(0,0,1));                // light half vector
    float nDotVP    = dot(normal, normalize(source.position));                 // normal . light direction
    float nDotHV    = max(0.f, dot(normal,  halfVector));                      // normal . light half vector
    float pf        = mix(0.f, pow(nDotHV, mat.shininess), step(0.f, nDotVP)); // power factor

    vec3 ambient    = source.ambient;
    vec3 diffuse    = source.diffuse * nDotVP;
    vec3 specular   = source.specular * pf;
    vec3 sceneColor = mat.emission + mat.ambient * model.ambient;

    return clamp(sceneColor +
                 ambient  * mat.ambient +
                 diffuse  * mat.diffuse +
                 specular * mat.specular, 0.f, 1.f );
}

void main()
{             
     fragColor = vec4(shadeBlinnPhong(lightSource, lightModel, material, v_position, v_normal), alpha);
     if(texEnabled == true)
         fragColor = fragColor * texture2D(texUnit, v_texCoord2d);

     if(envMapEnabled && displayMode == 3) // Environment mapping
     {
         vec3 I = normalize(v_position - cameraPos);
         vec3 R = reflect(I, (v_reflectionNormal));
         vec3 worldR = inverse(mat3(viewMatrix)) * R;
         fragColor = mix(fragColor, vec4(texture(envMap, worldR).rgba), material.shininess/256);
     }
}
