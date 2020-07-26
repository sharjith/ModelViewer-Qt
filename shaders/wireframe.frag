#version 450 core

in vec3 v_position;
in vec3 v_normal;
in vec2 v_texCoord2d;

vec4 v_color;
uniform float f_alpha;
uniform bool b_texEnabled;
uniform sampler2D texUnit;
uniform bool b_SectionActive;
uniform bool b_wireframe;


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

vec3 shadeBlinnPhong(vec3 position, vec3 normal)
{
    vec3 halfVector = normalize(lightSource.position + vec3(0,0,1));                // light half vector
    float nDotVP    = dot(normal, normalize(lightSource.position));                 // normal . light direction
    float nDotHV    = max(0.f, dot(normal,  halfVector));                           // normal . light half vector
    float pf        = mix(0.f, pow(nDotHV, material.shininess), step(0.f, nDotVP)); // power factor

    vec3 ambient    = lightSource.ambient;
    vec3 diffuse    = lightSource.diffuse * nDotVP;
    vec3 specular   = lightSource.specular * pf;
    vec3 sceneColor = material.emission + material.ambient * lightModel.ambient;

    return clamp(sceneColor +
                 ambient  * material.ambient +
                 diffuse  * material.diffuse +
                 specular * material.specular, 0.f, 1.f );
}

// The mesh line settings
uniform struct LineInfo {
    float Width;
    vec4 Color;
} Line;
in vec3 GPosition;
in vec3 GNormal;
noperspective in vec3 GEdgeDistance;

layout( location = 0 ) out vec4 fragColor;

void main()
{
    vec4 v_color_front;
    vec4 v_color_back;

    // For wireframe
    // Find the smallest distance
    float d = min( GEdgeDistance.x, GEdgeDistance.y );
    d = min( d, GEdgeDistance.z );
    // Determine the mix factor with the line color
    float mixVal = smoothstep( Line.Width - 1, Line.Width + 1, d );
    // Mix the surface color with the line color
    if(b_wireframe == true)
    {
        v_color_front = vec4(shadeBlinnPhong(GPosition, GNormal), f_alpha);
        v_color_back = vec4(shadeBlinnPhong(GPosition, -GNormal), f_alpha);
        v_color_front = mix( Line.Color, v_color_front, mixVal);
        v_color_back = mix( Line.Color, v_color_back, mixVal);
    }
    else
    {
        v_color_front =vec4( shadeBlinnPhong(v_position, v_normal), f_alpha);
        v_color_back = vec4(shadeBlinnPhong(v_position, -v_normal), f_alpha);
    }

    if( gl_FrontFacing )
    {
        v_color = v_color_front;
    }
    else
    {
        if(b_SectionActive)
            v_color = v_color_back + 0.15;
        else
            v_color = v_color_back;
    }

    if(b_texEnabled == true)
        fragColor = v_color * texture2D(texUnit, v_texCoord2d);
    else
        fragColor = v_color;
}
