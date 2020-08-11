#version 450 core

in vec3 g_position;
in vec3 g_normal;
in vec2 g_texCoord2d;
noperspective in vec3 g_edgeDistance;


vec4 v_color;
uniform float alpha;
uniform bool texEnabled;
uniform sampler2D texUnit;
uniform samplerCube envMap;
uniform bool envMapEnabled;
uniform vec3 cameraPos;
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

vec3 shadeBlinnPhong(LightSource source, LightModel model, Material mat, vec3 position, vec3 normal)
{
    vec3 halfVector = normalize(source.position + vec3(0,0,1));                // light half vector
    float nDotVP    = dot(normal, normalize(source.position));                 // normal . light direction
    float nDotHV    = max(0.f, dot(normal,  halfVector));                           // normal . light half vector
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

layout( location = 0 ) out vec4 fragColor;

void main()
{
    vec4 v_color_front;
    vec4 v_color_back;


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

    if(envMapEnabled && displayMode == 3)
    {
        vec3 I = normalize(g_position - cameraPos);
        vec3 R = reflect(I, normalize(g_normal));
        //vec3 R = refract(I, normalize(g_normal), 0.658);
        fragColor = mix(fragColor, vec4(texture(envMap, R).rgba), material.shininess/256);
    }

    if(selected)
    {
        fragColor = mix(fragColor, vec4(1.0, .65, 0.0, 1.0), 0.5);
    }
}
