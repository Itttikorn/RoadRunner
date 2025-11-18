#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 3) in ivec4 aBoneIDs;
layout(location = 4) in vec4 aWeights;

uniform mat4 model;
uniform mat4 lightSpaceMatrix;
uniform bool isAnimated;
const int MAX_BONES = 100;
uniform mat4 finalBonesMatrices[MAX_BONES];

void main()
{
    vec4 worldPos;
    if (isAnimated) {
        vec4 skinnedPos = vec4(0.0);
        for (int i = 0; i < 4; ++i) {
            int id = aBoneIDs[i];
            float w = aWeights[i];
            if (id >= 0 && w > 0.0) {
                mat4 bone = finalBonesMatrices[id];
                skinnedPos += bone * vec4(aPos, 1.0) * w;
            }
        }
        worldPos = model * skinnedPos;
    } else {
        worldPos = model * vec4(aPos, 1.0);
    }

    gl_Position = lightSpaceMatrix * worldPos;
}