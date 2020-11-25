#version 450 core

// Adpated from https://learnopengl.com/

in vec3 g_position;
in vec3 g_normal;
in vec2 g_texCoord2d;
in vec3 g_tangent;
in vec3 g_bitangent;
noperspective in vec3 g_edgeDistance;
in vec3 g_reflectionPosition;
in vec3 g_reflectionNormal;
in vec3 g_tangentLightPos;
in vec3 g_tangentViewPos;
in vec3 g_tangentFragPos;

in GS_OUT_SHADOW {
    vec3 FragPos;
    vec3 Normal;
    vec2 TexCoords;
    vec4 FragPosLightSpace;
    vec3 cameraPos;
    vec3 lightPos;
} fs_in_shadow;

uniform float opacity;
uniform bool texEnabled;
uniform sampler2D texUnit;

// ADS light maps
uniform sampler2D texture_diffuse;
uniform sampler2D texture_specular;
uniform sampler2D texture_emissive;
uniform sampler2D texture_normal;
uniform sampler2D texture_height;
uniform sampler2D texture_opacity;
uniform bool hasDiffuseTexture = false;
uniform bool hasSpecularTexture = false;
uniform bool hasEmissiveTexture = false;
uniform bool hasNormalTexture = false;
uniform bool hasHeightTexture = false;
uniform bool hasOpacityTexture = false;
uniform bool opacityTextureInverted = false;

uniform samplerCube envMap;
uniform sampler2D shadowMap;
// IBL
uniform samplerCube irradianceMap;
uniform samplerCube prefilterMap;
uniform sampler2D brdfLUT;

// material parameters
uniform sampler2D albedoMap;
uniform sampler2D normalMap;
uniform sampler2D metallicMap;
uniform sampler2D roughnessMap;
uniform sampler2D heightMap;
uniform sampler2D aoMap;
uniform sampler2D opacityMap;
uniform bool hasAlbedoMap;
uniform bool hasMetallicMap;
uniform bool hasRoughnessMap;
uniform bool hasNormalMap;
uniform bool hasAOMap;
uniform bool hasHeightMap;
uniform float heightScale;
uniform bool hasOpacityMap;
uniform bool opacityMapInverted = false;

uniform bool envMapEnabled;
uniform bool shadowsEnabled;
uniform float shadowSamples;
uniform vec3 cameraPos;
uniform mat4 viewMatrix;
uniform bool sectionActive;
uniform int displayMode;
uniform int renderingMode;
uniform bool selected;
uniform vec4 reflectColor;
uniform bool floorRendering;
uniform bool lockLightAndCamera = true;
uniform bool hdrToneMapping = false;
uniform bool gammaCorrection = false;
uniform float screenGamma = 2.2;

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
    bool  metallic;
};
uniform Material material;

struct PBRLighting {
    vec3 albedo;
    float metallic;
    float roughness;
    float ambientOcclusion;
};
uniform PBRLighting pbrLighting;

const float PI = 3.14159265359;

layout( location = 0 ) out vec4 fragColor;

float   calculateShadow(vec4 fragPosLightSpace);
vec4    shadeBlinnPhong(LightSource source, LightModel model, Material mat, vec3 position, vec3 normal);
vec4    calculatePBRLighting(int renderMode, float side);
void    applyEnvironmentMapping(float alpha);

vec3    getNormalFromMap();
mat3    getTBNFromMap();
float   distributionGGX(vec3 N, vec3 H, float roughness);
float   geometrySchlickGGX(float NdotV, float roughness);
float   geometrySmith(vec3 N, vec3 V, vec3 L, float roughness);
vec3    fresnelSchlick(float cosTheta, vec3 F0);
vec3    fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness);
vec2    parallaxMapping(vec2 texCoords, vec3 viewDir, sampler2D map);
vec3    calcBumpedNormal(sampler2D map, vec2 texCoord);

void main()
{
    vec4 v_color_front;
    vec4 v_color_back;
    vec4 v_color;

    if(renderingMode == 0)
    {
        v_color_front = shadeBlinnPhong(lightSource, lightModel, material, g_position, g_normal);
        v_color_back  = shadeBlinnPhong(lightSource, lightModel, material, g_position, -g_normal);
    }
    else
    {
        v_color_front = calculatePBRLighting(renderingMode, 1.0f);
        v_color_back  = calculatePBRLighting(renderingMode, -1.0f);
    }

    if( gl_FrontFacing )
    {
        v_color = v_color_front;
    }
    else
    {
        if(sectionActive)
            v_color = v_color_back + 0.15f;
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
        fragColor = vec4(1.0f, 1.0f, 1.0f, 0.75f);
    }
    else // wireshaded
    {
        // Find the smallest distance
        float d = min(g_edgeDistance.x, g_edgeDistance.y );
        d = min( d, g_edgeDistance.z );

        float mixVal;
        if( d < Line.Width - 1.0f )
        {
            mixVal = 1.0f;
        } else if( d > Line.Width + 1.0f )
        {
            mixVal = 0.0f;
        }
        else
        {
            float x = d - (Line.Width - 1.0f);
            mixVal = exp2(-2.0f * (x*x));
        }

        if(texEnabled == true)
            fragColor = mix(v_color * texture2D(texUnit, g_texCoord2d), Line.Color, mixVal);
        else
            fragColor = mix(v_color, Line.Color, mixVal);
    }

    // Get alpha from maps if available
    float alpha = opacity;
    if(renderingMode == 0 && hasOpacityTexture)
    {
        if(opacityTextureInverted)
            alpha = 1.0f - texture(texture_opacity, g_texCoord2d).r;
        else
            alpha = texture(texture_opacity, g_texCoord2d).r;
    }
    applyEnvironmentMapping(alpha);

    if(selected)
    {   
        vec3 objectColor = vec3(1.0f, 0.6392156862745098f, 0.396078431372549f);        
        // ambient
        float ambientStrength = 1.0;
        vec3 ambient = ambientStrength * vec3(0.50f, 0.50f, 0.50f);
  	
        // diffuse 
        vec3 norm = normalize(g_normal);
        vec3 lightDir = normalize(lightSource.position - g_position);
        float diff = max(dot(norm, lightDir), 0.0);
        vec3 diffuse = diff * vec3(0.750f, 0.750f, 0.750f);
    
        // specular
        float specularStrength = 0.5;
        vec3 viewDir = normalize(cameraPos - g_position);
        vec3 reflectDir = reflect(-lightDir, norm);  
        float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32);
        vec3 specular = specularStrength * spec * vec3(0.20f, 0.20f, 0.20f);
        
        vec3 result = (ambient + diffuse + specular) * objectColor;
        fragColor = vec4(result, 1.0);

        //fragColor = mix(fragColor, vec4(1.0f, .65f, 0.0f, 1.0f), 0.5f);
    }
}

// ----------------------------------------------------------------------------
vec4 shadeBlinnPhong(LightSource source, LightModel model, Material mat, vec3 position, vec3 normal)
{    
    vec2 texCoords = g_texCoord2d;
    vec2 clippedTexCoord = texCoords;
    vec3 lightDir;
    vec3 viewDir;
    if(lockLightAndCamera)
    {
        lightDir = source.position;
        viewDir = vec3(0);
    }
    else
    {
        lightDir = source.position + cameraPos;
        viewDir = cameraPos;
    }

    if(hasNormalTexture)
    {
        // obtain normal from normal map in range [0,1]
        normal = calcBumpedNormal(texture_normal, g_texCoord2d);
    }
    /*if(hasHeightTexture)
    {
        // offset texture coordinates with Parallax Mapping
        if(lockLightAndCamera)
            viewDir = normalize(g_tangentViewPos);
        else
            viewDir = normalize(g_tangentViewPos - g_tangentFragPos);
        clippedTexCoord = parallaxMapping(g_texCoord2d,  viewDir, texture_height);
        clippedTexCoord = vec2(texCoords.x - floor(texCoords.x),texCoords.y - floor(texCoords.y));
        if(clippedTexCoord.x > 1.0 || clippedTexCoord.y > 1.0 || clippedTexCoord.x < 0.0 || clippedTexCoord.y < 0.0)
              discard;
        // obtain normal from normal map
        normal = texture(texture_normal, clippedTexCoord).rgb;   
        if(lockLightAndCamera)
            lightDir = normalize(g_tangentLightPos);
        else
            lightDir = normalize(g_tangentLightPos - g_tangentFragPos);
    }*/
    if(hasHeightTexture)
    {
        if(!lockLightAndCamera)
            viewDir = normalize(-g_tangentFragPos - g_tangentViewPos);
        else
            viewDir = normalize(g_tangentFragPos + g_tangentFragPos);
        float height = texture(texture_height, g_texCoord2d).r;
        height = height * 0.08f + (-0.01f);//scale + bias;
        clippedTexCoord = g_texCoord2d + (height * viewDir.xy);
        if(!lockLightAndCamera)
            lightDir = normalize(-g_tangentFragPos - g_tangentLightPos);
        else
            lightDir = normalize(g_tangentLightPos + g_tangentFragPos);
        normal = calcBumpedNormal(texture_normal, clippedTexCoord);
    }
        
    vec3 halfVector = normalize(lightDir + viewDir); // light half vector     
    float nDotVP    = dot(normal, normalize(lightDir + viewDir));                 // normal . light direction
    float nDotHV    = max(0.f, dot(normal,  halfVector));                      // normal . light half vector
    float pf        = mix(0.f, pow(nDotHV, mat.shininess), step(0.f, nDotVP)); // power factor

    vec3 ambient    = source.ambient;        
    vec3 diffuse    = source.diffuse * nDotVP;    
    vec3 specular   = source.specular * pf;    
   
    vec3 matAmbient = mat.ambient;
    if(hasDiffuseTexture)
    {
        matAmbient = texture2D(texture_diffuse, clippedTexCoord).rgb;
    }
    vec3 matDiffuse = mat.diffuse;
    if(hasDiffuseTexture)
    {
        matDiffuse = nDotVP * texture2D(texture_diffuse, clippedTexCoord).rgb;
    }
    vec3 matSpecular = mat.specular;
    if(hasSpecularTexture)
    {
        matSpecular = pf * texture2D(texture_specular, clippedTexCoord).rgb;
    }
    vec3 matEmissive = mat.emission;
    if(hasEmissiveTexture)
    {
        matEmissive = texture2D(texture_emissive, clippedTexCoord).rgb;
    }
    float alpha = opacity;
    if(hasOpacityTexture)
    {
        if(opacityTextureInverted)
            alpha = 1.0f - texture(texture_opacity, clippedTexCoord).r;
        else
            alpha = texture(texture_opacity, clippedTexCoord).r;
    }

    vec3 sceneColor = matEmissive + matAmbient * model.ambient;
    vec4 colorLinear;
    if(shadowsEnabled && displayMode == 3) // Shadow Mapping
    {
        float shadowFactor = calculateShadow(fs_in_shadow.FragPosLightSpace);
        colorLinear =  vec4(clamp(sceneColor +
                             (ambient  * matAmbient + 1 - shadowFactor) *
                             (diffuse  * matDiffuse +
                              specular * matSpecular), 0.f, 1.f ), alpha);
    }
    else
    {
        colorLinear =  vec4(clamp(sceneColor +
                             ambient  * matAmbient +
                             diffuse  * matDiffuse +
                             specular * matSpecular, 0.f, 1.f ), alpha);
    }

    // HDR tonemapping
    if(hdrToneMapping)
        colorLinear = colorLinear / (colorLinear + vec4(1.0));
    // gamma correct
    if(gammaCorrection)
        colorLinear = pow(colorLinear, vec4(1.0/screenGamma));

    return colorLinear;
}
// ----------------------------------------------------------------------------
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

    vec3 normal = normalize(fs_in_shadow.Normal);
    vec3 lightDir;
    if(lockLightAndCamera)
        lightDir = normalize(lightSource.position);
    else
        lightDir = normalize(lightSource.position + fs_in_shadow.cameraPos);
    float bias = max(0.05 * (1.0 - dot(normal, lightDir)), 0.005);

    // PCF - Percentage Closer Filtering
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

    shadow /= shadowSamples;

    // keep the shadow at 0.0 when outside the far_plane region of the light's frustum.
    if(projCoords.z > 1.0)
        shadow = 0.0;

    return shadow;
}

vec4 calculatePBRLighting(int renderMode, float side) // side 1 = front, -1 = back
{
    vec3 normal = g_normal * side;
    vec3 albedo;
    float metallic;
    float roughness;
    float ambientOcclusion;
    vec3 N; vec3 V; vec3 L;
    if(lockLightAndCamera)
    {
        V = normalize(lightSource.position);
        L = normalize(lightSource.position);
    }
    else
    {
        V = normalize(lightSource.position + cameraPos);
        L = normalize(lightSource.position + cameraPos);
    }
    vec2 texCoords = g_texCoord2d;
    vec2 clippedTexCoord = texCoords;

    if(renderMode == 1)
    {
        N = normalize(normal);
        albedo = pbrLighting.albedo;
        metallic = pbrLighting.metallic;
        roughness = pbrLighting.roughness;
        ambientOcclusion = pbrLighting.ambientOcclusion;
    }
    else
    {    
        if(hasNormalMap)
            N = calcBumpedNormal(normalMap, g_texCoord2d) * side;
        else
            N = normalize(normal);
        
        /*if(hasHeightMap)
        {
            // offset texture coordinates with Parallax Mapping
            vec3 viewDir = normalize(-g_tangentFragPos - g_tangentViewPos);
            clippedTexCoord = parallaxMapping(g_texCoord2d,  viewDir, heightMap);
            clippedTexCoord = vec2(texCoords.x - floor(texCoords.x),texCoords.y - floor(texCoords.y));
            if(clippedTexCoord.x > 1.0 || clippedTexCoord.y > 1.0 || clippedTexCoord.x < 0.0 || clippedTexCoord.y < 0.0)
                discard;
            // obtain normal from normal map
            //N = texture(normalMap, clippedTexCoord).rgb * side;
            N = calcBumpedNormal(normalMap, clippedTexCoord) * side;
            V = normalize(g_tangentLightPos);
            L = normalize(g_tangentLightPos);
        }*/
        if(hasHeightMap)
        {
            vec3 viewDir;
            if(lockLightAndCamera)
                viewDir = normalize(g_tangentViewPos - g_tangentFragPos);
            else
                viewDir = normalize(g_tangentLightPos + g_tangentFragPos);
            float height = texture(heightMap, g_texCoord2d).r;
            height = height * heightScale + (-0.01f);//scale + bias;            
            texCoords = g_texCoord2d + (height * viewDir.xy);
            clippedTexCoord = vec2(texCoords.x - floor(texCoords.x),texCoords.y - floor(texCoords.y));
            if(clippedTexCoord.x > 1.0 || clippedTexCoord.y > 1.0 || clippedTexCoord.x < 0.0 || clippedTexCoord.y < 0.0)
                discard;
            if(lockLightAndCamera)
            {
                L = normalize(g_tangentLightPos - g_tangentFragPos);
                V = normalize(g_tangentLightPos - g_tangentFragPos);
            }
            else
            {
                L = normalize(g_tangentLightPos + g_tangentFragPos);
                V = normalize(g_tangentLightPos + g_tangentFragPos);
            }
            N = calcBumpedNormal(normalMap, clippedTexCoord) * side;            
        }

        // material properties
        if(hasAlbedoMap)
            albedo = pow(texture(albedoMap, clippedTexCoord).rgb, vec3(2.2));
        else
            albedo = pbrLighting.albedo;

        if(hasMetallicMap)
            metallic = texture(metallicMap, clippedTexCoord).r;
        else
            metallic = pbrLighting.metallic;

        if(hasRoughnessMap)
            roughness = texture(roughnessMap, clippedTexCoord).r;
        else
            roughness = pbrLighting.roughness;

        if(hasAOMap)
            ambientOcclusion = texture(aoMap, clippedTexCoord).r;
        else
            ambientOcclusion = pbrLighting.ambientOcclusion;
    }
    
    // calculate reflectance at normal incidence; if dia-electric (like plastic) use F0
    // of 0.04 and if it's a metal, use the albedo color as F0 (metallic workflow)
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);

    // reflectance equation
    vec3 Lo = vec3(0.0);

    // calculate light radiance    
    vec3 H = normalize(V + L);
    float distance = length(lightSource.position - vec3(0));
    float attenuation = 1.0 / (distance * distance);
    float lightIntensity = 1000.0f;
    vec3 lightColor = vec3(3.0f, 3.0f, 3.0f) * lightIntensity;
    //vec3 radiance = lightColor * attenuation;//(lightSource.ambient + lightSource.diffuse + lightSource.specular);
    vec3 radiance;
    if(shadowsEnabled && displayMode == 3)
    {
        float shadowFactor = calculateShadow(fs_in_shadow.FragPosLightSpace);
        radiance = (lightSource.ambient + 1- shadowFactor)  * (lightSource.diffuse + lightSource.specular);
    }
    else
    {
        radiance = (lightSource.ambient + lightSource.diffuse + lightSource.specular);
    }

    // Cook-Torrance BRDF
    float NDF = distributionGGX(N, H, roughness);
    float G   = geometrySmith(N, V, L, roughness);
    vec3 F    = fresnelSchlick(clamp(dot(H, V), 0.0, 1.0), F0);

    vec3 nominator    = NDF * G * F;
    float denominator = 4 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0);
    vec3 specular = nominator / max(denominator, 0.001); // prevent divide by zero for NdotV=0.0 or NdotL=0.0

    // kS is equal to Fresnel
    vec3 kS = F;
    // for energy conservation, the diffuse and specular light can't
    // be above 1.0 (unless the surface emits light); to preserve this
    // relationship the diffuse component (kD) should equal 1.0 - kS.
    vec3 kD = vec3(1.0) - kS;
    // multiply kD by the inverse metalness such that only non-metals
    // have diffuse lighting, or a linear blend if partly metal (pure metals
    // have no diffuse light).
    kD *= 1.0 - metallic;

    // scale light by NdotL
    float NdotL = max(dot(N, L), 0.0);

    // add to outgoing radiance Lo
    Lo += (kD * albedo / PI + specular) * radiance * NdotL;  // note that we already multiplied the BRDF by the Fresnel (kS) so we won't multiply by kS again

    // ambient lighting (note that the IBL will replace
    // this ambient lighting with environment lighting).
    vec3 ambient;
    // ambient lighting (we now use IBL as the ambient term)
    vec3 irradiance = texture(irradianceMap, N).rgb;
    vec3 diffuse      = irradiance * albedo;

    if(displayMode == 3)
    {        
        if(envMapEnabled)
        {
            vec3 F = fresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);

            kS = F;
            kD = 1.0 - kS;
            kD *= 1.0 - metallic;
                        
            vec3 I = normalize(cameraPos - g_reflectionPosition);
            vec3 R = refract(-I, normalize(-g_reflectionNormal), 1.0f);
            
            // sample both the pre-filter map and the BRDF lut and combine them together as per the Split-Sum approximation to get the IBL specular part.
            const float MAX_REFLECTION_LOD = 4.0;
            vec3 prefilteredColor = textureLod(prefilterMap, R,  roughness * MAX_REFLECTION_LOD).rgb;
            vec2 brdf  = texture(brdfLUT, vec2(max(dot(N, V), 0.0), roughness)).rg;
            vec3 specular = prefilteredColor * (F * brdf.x + brdf.y);

            ambient = (kD * diffuse + specular) * ambientOcclusion;
        }
        else
        {            
            kS = fresnelSchlick(max(dot(N, V), 0.0), F0);
            kD = 1.0 - kS;
            kD *= 1.0 - metallic;
            ambient = (kD * diffuse) * ambientOcclusion;
        }        
    }
    else
    {        
        ambient = ((lightSource.ambient * diffuse)  + specular) * ambientOcclusion;
    }

    // Emission map
    vec3 matEmissive = material.emission;
    if(hasEmissiveTexture)
    {
        matEmissive = texture2D(texture_emissive, clippedTexCoord).rgb;
    }

    vec3 color = matEmissive + ambient + Lo;

    // HDR tonemapping
    if(hdrToneMapping)
        color = color / (color + vec3(1.0));
    // gamma correct
    if(gammaCorrection)
        color = pow(color, vec3(1.0/screenGamma));

    float alpha = opacity;
    if(hasOpacityMap)
    {
        if(opacityMapInverted)
            alpha = 1.0f - texture(opacityMap, clippedTexCoord).r;
        else
            alpha = texture(opacityMap, clippedTexCoord).r;
    }

    return vec4(color, alpha);
}

void applyEnvironmentMapping(float alpha)
{
    if(envMapEnabled && displayMode == 3) // Environment mapping
    {

        if(alpha < 1.0f && !floorRendering) // Transparent - refract
        {
            vec4 colour = fragColor;
            vec3 I = normalize(g_reflectionPosition - cameraPos);
            vec3 R = refract(I, normalize(g_reflectionNormal), 1.0f - alpha);
            if(texEnabled == true)
                fragColor = mix(texture2D(texUnit, g_texCoord2d), vec4(texture(envMap, R).rgb, 1.0f - alpha), 1.0f - alpha);
            else
                fragColor = vec4(texture(envMap, R).rgb, 1.0f - alpha);
            fragColor = mix(fragColor, colour, alpha/1.0f);
        }
        else if(renderingMode == 0)// Opaque - Reflect
        {
            vec3 I = normalize(cameraPos - g_reflectionPosition);
            vec3 R = refract(-I, normalize(-g_reflectionNormal), 1.0f); // inverted refraction for reflection
            float factor =  material.metallic ? length(material.specular) : length(material.diffuse);
            fragColor = mix(fragColor, vec4(texture(envMap, R).rgb, 1.0f), material.shininess/128.0f * factor);
        }
    }
}

// ----------------------------------------------------------------------------
// Easy trick to get tangent-normals to world-space to keep PBR code simplified.
// Don't worry if you don't get what's going on; you generally want to do normal
// mapping the usual way for performance anways; I do plan make a note of this
// technique somewhere later in the normal mapping tutorial.
vec3 getNormalFromMap()
{
    vec3 tangentNormal = texture(normalMap, g_texCoord2d).xyz * 2.0 - 1.0;

    vec3 Q1  = dFdx(g_position);
    vec3 Q2  = dFdy(g_position);
    vec2 st1 = dFdx(g_texCoord2d);
    vec2 st2 = dFdy(g_texCoord2d);

    vec3 N   = normalize(g_normal);
    vec3 T  = normalize(Q1*st2.t - Q2*st1.t);
    vec3 B  = -normalize(cross(N, T));
    mat3 TBN = mat3(T, B, N);

    return normalize(TBN * tangentNormal);
}

mat3 getTBNFromMap()
{
    vec3 tangentNormal = texture(normalMap, g_texCoord2d).xyz * 2.0 - 1.0;

    vec3 Q1  = dFdx(g_position);
    vec3 Q2  = dFdy(g_position);
    vec2 st1 = dFdx(g_texCoord2d);
    vec2 st2 = dFdy(g_texCoord2d);

    vec3 N   = normalize(g_normal);
    vec3 T  = normalize(Q1*st2.t - Q2*st1.t);
    vec3 B  = -normalize(cross(N, T));
    mat3 TBN = mat3(T, B, N);

    return TBN;
}

// ----------------------------------------------------------------------------
float distributionGGX(vec3 N, vec3 H, float roughness)
{
    float a = roughness*roughness;
    float a2 = a*a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH*NdotH;

    float nom   = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return nom / max(denom, 0.001); // prevent divide by zero for roughness=0.0 and NdotH=1.0
}
// ----------------------------------------------------------------------------
float geometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r*r) / 8.0;

    float nom   = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return nom / denom;
}
// ----------------------------------------------------------------------------
float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = geometrySchlickGGX(NdotV, roughness);
    float ggx1 = geometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}
// ----------------------------------------------------------------------------
vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}
// ----------------------------------------------------------------------------
vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - cosTheta, 5.0);
}

vec2 parallaxMapping(vec2 texCoords, vec3 viewDir, sampler2D map)
{
    float height =  texture(map, texCoords).r;
    return texCoords - viewDir.xy * (height * heightScale);
}

// http://ogldev.atspace.co.uk/www/tutorial26/tutorial26.html
vec3 calcBumpedNormal(sampler2D map, vec2 texCoord)
{
    vec3 normal = normalize(g_normal);
    vec3 tangent = normalize(g_tangent);
    tangent = normalize(tangent - dot(tangent, normal) * normal);
    vec3 bitangent = cross(tangent, normal);
    vec3 bumpMapNormal = texture(map, texCoord).xyz;
    bumpMapNormal = 2.0 * bumpMapNormal - vec3(1.0, 1.0, 1.0);
    vec3 newNormal;
    mat3 TBN = mat3(tangent, bitangent, normal);
    newNormal = TBN * bumpMapNormal;
    newNormal = normalize(newNormal);
    return newNormal;
}
