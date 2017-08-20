#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 1) uniform sampler2D diffuseSampler;
layout(binding = 2) uniform sampler2D specularSampler;
layout(binding = 3) uniform sampler2D normalSampler;

layout(location = 0) in vec3 fragPos;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec3 fragTangent;

layout(location = 0) out vec4 outPosition;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outAlbedo;

void main() {
	outPosition = vec4(fragPos, 1.0f);

	vec3 normal = normalize(fragNormal);
	normal.y = -normal.y;
	vec3 tangent = normalize(fragTangent);
	vec3 bitangent = cross(normal, tangent);
	mat3 matTBN = mat3(tangent, bitangent, normal);
	vec3 tangentSpaceNormal = matTBN * 
		normalize(texture(normalSampler, fragTexCoord).xyz * 2.0 - vec3(1.0));
	outNormal = vec4(tangentSpaceNormal, 1.0f);

	outAlbedo = texture(diffuseSampler, fragTexCoord);
	outAlbedo.w = 0.05f; //Specular power
}
