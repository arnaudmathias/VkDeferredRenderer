#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0) uniform UniformBufferObject {
	mat4 model;
	mat4 view;
	mat4 proj;
} ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inTangent;

layout(location = 0) out vec3 fragPos;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragNormal;
layout(location = 3) out vec3 fragTangent;

out gl_PerVertex {
	vec4 gl_Position;
};

void main() {
	gl_Position = ubo.proj * ubo.view * ubo.model * vec4(inPosition, 1.0);

	fragPos = vec3(ubo.model * vec4(inPosition, 1.0));
	fragPos.y = -fragPos.y;

	fragTexCoord = inTexCoord;
	fragTexCoord.y = 1.0f - fragTexCoord.y;

	mat3 matNormal = transpose(inverse(mat3(ubo.model)));
	fragNormal = matNormal * normalize(inNormal);
	fragTangent = matNormal * normalize(inTangent);
}
