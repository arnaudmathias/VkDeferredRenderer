#version 450
#extension GL_ARB_separate_shader_objects : enable

layout (input_attachment_index = 0, binding = 0) uniform subpassInput positionInput;
layout (input_attachment_index = 1, binding = 1) uniform subpassInput normalInput;
layout (input_attachment_index = 2, binding = 2) uniform subpassInput albedoInput;

layout(location = 0) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

struct Light {
	vec4 position;
	vec3 color;
	float radius;
};

layout(binding = 3) uniform UniformBufferObject {
	vec4	viewPosition;
	Light	lights[6];
} ubo;

void main() {
	vec3 fragPos = subpassLoad(positionInput).rgb;
	vec3 normal = subpassLoad(normalInput).rgb;
	vec4 albedo = subpassLoad(albedoInput);

	vec3 fragColor = albedo.rgb * 0.20f;

	for (int i = 0; i < 6; i++) {
		vec3 L = ubo.lights[i].position.xyz - fragPos;
		// Distance from light to fragment position
		float dist = length(L);

		// Viewer to fragment
		vec3 V = ubo.viewPosition.xyz - fragPos;
		V = normalize(V);
		
		// Light to fragment
		L = normalize(L);

		// Attenuation
		float atten = ubo.lights[i].radius / (pow(dist, 2.0) + 1.0);

		// Diffuse part
		vec3 N = normalize(normal);
		float NdotL = max(0.0, dot(N, L));
		vec3 diff = ubo.lights[i].color * albedo.rgb * NdotL * atten;

		// Specular part
		// Specular map values are stored in alpha of albedo mrt
		vec3 R = reflect(-L, N);
		float NdotR = max(0.0, dot(R, V));
		vec3 spec = ubo.lights[i].color * albedo.a * pow(NdotR, 16.0) * atten;

		fragColor += diff + spec;	
	}

	//TODO: light calc
	outColor = vec4(fragColor, 1.0f);

}
