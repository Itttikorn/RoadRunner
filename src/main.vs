#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;
// These are in the vertex buffer but we won't use them
layout (location = 3) in vec3 aTangent;
layout (location = 4) in vec3 aBitangent;
// Animation attributes - CORRECT LOCATIONS NOW!
layout (location = 5) in ivec4 aBoneIDs;
layout (location = 6) in vec4  aWeights;

out VS_OUT {
    vec2 TexCoords;
    vec3 FragPos;
    vec3 Normal;
} vs_out;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

// Animation uniforms
const int MAX_BONES = 100;
uniform mat4 finalBonesMatrices[MAX_BONES];
uniform int isAnimated; // 1 if animated, 0 if static

void main()
{
    vec4 localPos = vec4(aPos, 1.0);
    vec3 localNormal = aNormal;

    if (isAnimated == 1) {
        // Calculate total weight
        float totalWeight = aWeights.x + aWeights.y + aWeights.z + aWeights.w;
        
        // Apply bone transformations if we have valid weights
        if (totalWeight > 0.01) {
            mat4 boneTransform = mat4(0.0);
            
            for (int i = 0; i < 4; ++i) {
                if (aWeights[i] > 0.0 && aBoneIDs[i] >= 0 && aBoneIDs[i] < MAX_BONES) {
                    boneTransform += finalBonesMatrices[aBoneIDs[i]] * aWeights[i];
                }
            }
            
            localPos = boneTransform * localPos;
            localNormal = mat3(boneTransform) * localNormal;
        }
    }

    vec4 worldPos = model * localPos;
    vs_out.FragPos   = worldPos.xyz;
    vs_out.Normal    = mat3(transpose(inverse(model))) * localNormal;
    vs_out.TexCoords = aTexCoords;
    gl_Position = projection * view * worldPos;
}