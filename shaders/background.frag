#version 400 core

uniform vec4 top_color;
uniform vec4 bot_color;
in vec2 v_uv;

out vec4 frag_color;
void main()
{
    frag_color = bot_color * (1 - v_uv.y) + top_color * v_uv.y;
}
