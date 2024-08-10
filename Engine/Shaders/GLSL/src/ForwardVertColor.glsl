#include "Common.glsl"

layout (set = 0, binding = 0) uniform GlobalUniformBuffer 
{
    GlobalUniforms global;
};

layout (set = 1, binding = 0) uniform GeometryUniformBuffer 
{
	GeometryUniforms geometry;
};

#if INSTANCED_DRAW
layout (std140, set = 1, binding = 1) buffer GeometryInstanceBuffer
{
    MeshInstanceData instanceData[];
};
#endif

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexcoord0;
layout(location = 2) in vec2 inTexcoord1;
layout(location = 3) in vec3 inNormal;
layout(location = 4) in vec4 inColor;

layout(location = 0) out vec3 outPosition;
layout(location = 1) out vec2 outTexcoord0;
layout(location = 2) out vec2 outTexcoord1;
layout(location = 3) out vec3 outNormal;
layout(location = 4) out vec4 outColor;

out gl_PerVertex 
{
    vec4 gl_Position;
};

void main()
{
#if INSTANCED_DRAW
    mat4 worldMatrix = instanceData[gl_InstanceIndex].mWorldMatrix;
    mat4 normalMatrix = instanceData[gl_InstanceIndex].mNormalMatrix;
    mat4 wvpMatrix = instanceData[gl_InstanceIndex].mWvpMatrix;
#else
    mat4 worldMatrix = geometry.mWorldMatrix;
    mat4 normalMatrix = geometry.mNormalMatrix;
    mat4 wvpMatrix = geometry.mWVP;
#endif
    
    gl_Position = wvpMatrix * vec4(inPosition, 1.0);
    outPosition = (worldMatrix * vec4(inPosition, 1.0)).xyz;    
    outTexcoord0 = inTexcoord0;    
    outTexcoord1 = inTexcoord1;    
    outNormal = normalize((normalMatrix * vec4(inNormal, 0.0)).xyz);
    outColor = SrgbToLinear(inColor);
}
