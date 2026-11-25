#version 450
layout(push_constant) uniform Draw { vec4 rect; vec4 tint; vec2 screen; vec2 atlasOrigin; float radius; int kind; };
layout(location=0) in vec2 pixel;
layout(location=0) out vec4 outputColor;
layout(set=0,binding=0) uniform sampler2D atlas;
void main(){
 float coverage=1.0;
 if(kind==1){
   float r=min(radius,min(floor(rect.z/2.0),floor(rect.w/2.0)));
   vec2 d=max(vec2(0),vec2(r)-min(pixel-rect.xy,rect.xy+rect.zw-pixel));
   if(d.x>0.0 && d.y>0.0) coverage=floor(clamp(r+0.5-length(d),0.0,1.0)*255.0)/255.0;
 } else if(kind==2) {
   coverage=texelFetch(atlas,ivec2(atlasOrigin+floor(pixel-rect.xy)),0).r;
 }
 outputColor=vec4(tint.rgb,tint.a*coverage);
}
