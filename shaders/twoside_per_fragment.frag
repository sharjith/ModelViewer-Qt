#version 450 core

in vec3 g_position;
in vec3 g_normal;
in vec2 g_texCoord2d;
noperspective in vec3 g_edgeDistance;
in vec4 g_fragPosLightSpace;
in vec3 g_reflectionNormal;
in vec4 g_clipSpace;

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

float calculateShadow(vec4 fragPosLightSpace)
{
    // perform perspective divide
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    // transform to [0,1] range
    projCoords = projCoords * 0.5 + 0.5;
    // get closest depth value from light's perspective (using [0,1] range fragPosLight as coords)
    float closestDepth = texture(shadowMap, projCoords.xy).r;
    // get depth of current fragment from light's perspective
    float currentDepth = projCoords.z;
    // calculate bias (based on depth map resolution and slope)
    vec3 normal = normalize(g_normal);
    vec3 lightDir = normalize(lightSource.position - g_position);
    float bias = max(0.05 * (1.0 - dot(normal, lightDir)), 0.005);
    // check whether current frag pos is in shadow
    //float shadow = currentDepth - bias > closestDepth  ? 1.0 : 0.0;
    // PCF
    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(shadowMap, 0);
    for(int x = -1; x <= 1; ++x)
    {
        for(int y = -1; y <= 1; ++y)
        {
            float pcfDepth = texture(shadowMap, projCoords.xy + vec2(x, y) * texelSize).r;
            shadow += currentDepth - bias > pcfDepth  ? 1.0 : 0.0;
        }
    }
    shadow /= 100.0;

    // keep the shadow at 0.0 when outside the far_plane region of the light's frustum.
    if(projCoords.z > 1.0)
        shadow = 0.0;

    return shadow;
}

void main()
{
    vec4 v_color_front;
    vec4 v_color_back;
    vec4 v_color;

    v_color_front = vec4(shadeBlinnPhong(lightSource, lightModel, material, g_position, g_normal), alpha);
    v_color_back  = vec4(shadeBlinnPhong(lightSource, lightModel, material, g_position, -g_normal), alpha);

    if( gl_FrontFacing )
    {
        v_color = v_color_front;
    }
    else
    {
        if(sectionActive)
            v_color = v_color_back + 0.15;
        else
            v_color = v_color_back;
    }

    if(displayMode == 0 || displayMode == 3) // shaded
    {
        if(texEnabled == true)
            fragColor = v_color * texture2D(texUnit, g_texCoord2d);
        else
            fragColor = v_color;
    }
    else if(displayMode == 1) // wireframe
    {
        fragColor = vec4(1.0, 1.0, 1.0, 0.75);
    }
    else // wireshaded
    {
        // Find the smallest distance
        float d = min(g_edgeDistance.x, g_edgeDistance.y );
        d = min( d, g_edgeDistance.z );

        float mixVal;
        if( d < Line.Width - 1 )
        {
            mixVal = 1.0;
        } else if( d > Line.Width + 1 )
        {
            mixVal = 0.0;
        }
        else
        {
            float x = d - (Line.Width - 1);
            mixVal = exp2(-2.0 * (x*x));
        }

        if(texEnabled == true)
            fragColor = mix(v_color * texture2D(texUnit, g_texCoord2d), Line.Color, mixVal);
        else
            fragColor = mix(v_color, Line.Color, mixVal);
    }


    if(envMapEnabled && displayMode == 3) // Environment mapping
    {
        vec3 I = normalize(g_position - cameraPos);
        vec3 R = reflect(I, (g_reflectionNormal));
        vec3 worldR = inverse(mat3(viewMatrix)) * R;
        fragColor = mix(fragColor, vec4(texture(envMap, worldR).rgba), material.shininess/256);        
    }


    if(shadowsEnabled && displayMode == 3) // Shadow Mapping
    {
        vec3 color = fragColor.rgb;
        vec3 normal = normalize(g_normal);
        vec3 lightColor = lightSource.ambient;
        // ambient
        vec3 ambient = lightSource.ambient;
        // diffuse
        vec3 lightDir = normalize(lightSource.position - g_position);
        float diff = max(dot(lightDir, normal), 0.0);
        vec3 diffuse = lightSource.diffuse;
        // specular
        vec3 viewDir = normalize(cameraPos - lightSource.position);
        vec3 reflectDir = reflect(-lightDir, normal);
        float spec = 0.0;
        vec3 halfwayDir = normalize(lightDir + viewDir);
        spec = pow(max(dot(normal, halfwayDir), 0.0), 64.0);
        vec3 specular = spec * lightColor;
        // calculate shadow
        float shadow = calculateShadow(g_fragPosLightSpace);
        vec3 lighting = (ambient + (1.0 - shadow) * (diffuse + specular)) * color;

        fragColor = vec4(lighting, alpha);

        if(reflectionsEnabled)
        {
            vec2 ndc = (g_clipSpace.xy/g_clipSpace.w)/2.0 + 0.5;
            vec2 reflectCoord = vec2(ndc.x, -ndc.y);
            //fragColor = mix(vec4(texture2D(reflectionMap, ndc).rgba),  fragColor, 0.5);
        }
    }
    
    if(selected)
    {
        fragColor = mix(fragColor, vec4(1.0, .65, 0.0, 1.0), 0.5);
    }
}
