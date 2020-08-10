#version 450 core

vec3 v_color;
uniform bool texEnabled;
uniform sampler2D texUnit;
		
in vec2 v_texCoord2d;
in vec3 v_color_front;
in vec3 v_color_back;
in float f_alpha;
uniform bool sectionActive;

layout( location = 0 ) out vec4 fragColor;
		
void main()
{
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

        if(texEnabled= true)
                fragColor = vec4(v_color, f_alpha) * texture2D(texUnit, v_texCoord2d);
	else
                fragColor = vec4(v_color, f_alpha);
}
