#version 450
#extension GL_ARB_separate_shader_objects : enable

#include "Common.glsl"

layout (set = 0, binding = 0) uniform GlobalUniformBuffer 
{
	GlobalUniforms global;
};

layout (set = 1, binding = 0) uniform GeometryUniformBuffer 
{
	GeometryUniforms geometry;
};

layout (location = 0) out uint outId;

void main()
{
	uint nodeIdx = (geometry.mHitCheckId << 16);
	uint instanceIdx = 0;
	uint hitCheckOut = (nodeIdx | instanceIdx);
	outId = hitCheckOut;
}