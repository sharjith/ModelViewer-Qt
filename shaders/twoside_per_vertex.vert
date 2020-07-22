#version 400

in vec3 vertexPosition;
in vec3 vertexNormal;
in vec2 texCoord2d;

uniform float f_alpha;
out float alpha;

uniform mat4 modelViewMatrix;
uniform mat3 normalMatrix;
uniform mat4 projectionMatrix;
uniform vec4 clipPlaneX;
uniform vec4 clipPlaneY;
uniform vec4 clipPlaneZ;

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

out vec3 v_color_front;
out vec3 v_color_back;
out vec2 v_texCoord2d;
out vec4 MVP;				

vec3 shadeBlinnPhong(vec3 position, vec3 normal, Material mat)
{
	vec3 halfVector = normalize(lightSource.position + vec3(0,0,1));                // light half vector          
	float nDotVP    = dot(normal, normalize(lightSource.position));                 // normal . light direction   
	float nDotHV    = max(0.f, dot(normal,  halfVector));                           // normal . light half vector 
	float pf        = mix(0.f, pow(nDotHV, mat.shininess), step(0.f, nDotVP)); // power factor               

	vec3 ambient    = lightSource.ambient;
	vec3 diffuse    = lightSource.diffuse * nDotVP;
	vec3 specular   = lightSource.specular * pf;
	vec3 sceneColor = mat.emission + mat.ambient * lightModel.ambient;

	return clamp(sceneColor +                             
			ambient  * mat.ambient +                 
			diffuse  * mat.diffuse +                 
			specular * mat.specular, 0.f, 1.f );
}


void main()
{

	vec3 normal     = normalize(normalMatrix * vertexNormal);                       // normal vector              
	vec3 position   = vec3(modelViewMatrix * vec4(vertexPosition, 1));              // vertex pos in eye coords   

	v_color_front = shadeBlinnPhong(position, normal, material);
	v_color_back = shadeBlinnPhong(position, -normal, material);

	v_texCoord2d = texCoord2d;

	alpha = f_alpha;
	gl_Position = projectionMatrix * modelViewMatrix * vec4(vertexPosition, 1);
	
	gl_ClipDistance[0] = dot(clipPlaneX, modelViewMatrix* vec4(vertexPosition, 1));
	gl_ClipDistance[1] = dot(clipPlaneY, modelViewMatrix* vec4(vertexPosition, 1));
	gl_ClipDistance[2] = dot(clipPlaneZ, modelViewMatrix* vec4(vertexPosition, 1));
}

