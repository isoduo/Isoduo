#version 450
layout(push_constant) uniform Draw { vec4 rect; vec4 tint; vec2 screen; vec2 atlasOrigin; float radius; int kind; };
layout(location=0) out vec2 pixel;
void main(){
 vec2 corners[6]=vec2[6](vec2(0,0),vec2(1,0),vec2(0,1),vec2(0,1),vec2(1,0),vec2(1,1));
 pixel=rect.xy+corners[gl_VertexIndex]*rect.zw;
 gl_Position=vec4(pixel.x/screen.x*2.0-1.0,pixel.y/screen.y*2.0-1.0,0,1);
}
