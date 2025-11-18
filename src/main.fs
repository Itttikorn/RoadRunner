#version 330 core
out vec4 FragColor;

in VS_OUT {
    vec2 TexCoords;
    vec3 FragPos;
    vec3 Normal;
} fs_in;

uniform sampler2D texture_diffuse1;

void main()
{    
    // Sample the texture (now contains material color)
    vec4 texColor = texture(texture_diffuse1, fs_in.TexCoords);
    
    // Simple lighting with higher ambient
    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));
    vec3 normal = normalize(fs_in.Normal);
    
    // Higher ambient light so colors are more visible
    float ambient = 0.5;
    float diff = max(dot(normal, lightDir), 0.0);
    float lighting = ambient + diff * 0.5;
    
    // Apply lighting to texture color
    FragColor = vec4(texColor.rgb * lighting, texColor.a);
}