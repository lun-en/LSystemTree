#version 430

in vec2 TexCoord;
in vec3 Normal;
in vec3 FragPos;

out vec4 color;

uniform sampler2D ourTexture;

uniform vec3 viewPos;

struct Material {
    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
    float shininess;
    float reflectivity;
}; 

struct DirectionLight
{
    int enable;
    vec3 direction;
    vec3 lightColor;
};

struct PointLight {
    int enable;
    vec3 position;  
    vec3 lightColor;

    float constant;
    float linear;
    float quadratic;
};

struct Spotlight {
    int enable;
    vec3 position;
    vec3 direction;
    vec3 lightColor;
    float cutOff;

    // Paramters for attenuation formula
    float constant;
    float linear;
    float quadratic;      
}; 

uniform Material material;
uniform DirectionLight dl;
uniform PointLight pl;
uniform Spotlight sl;
uniform samplerCube skybox;

// TODO#3-4: fragment shader
// Note:
//           1. how to write a fragment shader:
//              a. The output is FragColor (any var is OK)
//           2. Calculate
//              a. For direct light, lighting = ambient + diffuse + specular
//              b. For point light & spot light, lighting = ambient + attenuation * (diffuse + specular)
//              c. Final color = direct + point + spot if all three light are enabled
//           3. attenuation
//              a. Use formula from slides 'shading.ppt' page 20
//           4. For each light, ambient, diffuse and specular color are the same
//           5. diffuse = kd * max(normal vector dot light direction, 0.0)
//           6. specular = ks * pow(max(normal vector dot halfway direction), 0.0), shininess);
//           7. we've set all light parameters for you (see context.h) and definition in fragment shader
//           8. You should pre calculate normal matrix (trasfer normal from model local space to world space) 
//              in light.cpp rather than in shaders

// --- Directional Light ---
vec3 calculateDirectionLight() {
    if (dl.enable == 0) return vec3(0.0);

    // Ambient
    vec3 ambient = dl.lightColor * material.ambient;

    // Diffuse
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(-dl.direction);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = dl.lightColor * (diff * material.diffuse);

    // Specular
    vec3 viewDir = normalize(viewPos - FragPos);
    vec3 reflectDir = reflect(-lightDir, norm);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), material.shininess);
    vec3 specular = dl.lightColor * (spec * material.specular);

    return ambient + diffuse + specular;
}

// --- Point Light ---
vec3 calculatePointLight() {
    if (pl.enable == 0) return vec3(0.0);

    // Attenuation vars
    vec3 lightVec = pl.position - FragPos;
    float distance = length(lightVec);
    vec3 lightDir = normalize(lightVec);
    float attenuation = 1.0 / (pl.constant + pl.linear * distance + pl.quadratic * (distance * distance));

    // Ambient
    vec3 ambient = pl.lightColor * material.ambient;

    // Diffuse
    vec3 norm = normalize(Normal);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = pl.lightColor * (diff * material.diffuse);

    // Specular
    vec3 viewDir = normalize(viewPos - FragPos);
    vec3 reflectDir = reflect(-lightDir, norm);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), material.shininess);
    vec3 specular = pl.lightColor * (spec * material.specular);

    return (ambient + diffuse + specular) * attenuation;
}

// --- Spot Light ---
vec3 calculateSpotLight() {
    if (sl.enable == 0) return vec3(0.0);

    vec3 lightVec = sl.position - FragPos;
    float distance = length(lightVec);
    vec3 lightDir = normalize(lightVec);

    // Check Angle
    float theta = dot(lightDir, normalize(-sl.direction));

    // Attenuation
    float attenuation = 1.0 / (sl.constant + sl.linear * distance + sl.quadratic * (distance * distance));

    if(theta > sl.cutOff) {
        // Inside Cone
        vec3 ambient = sl.lightColor * material.ambient;

        vec3 norm = normalize(Normal);
        float diff = max(dot(norm, lightDir), 0.0);
        vec3 diffuse = sl.lightColor * (diff * material.diffuse);

        vec3 viewDir = normalize(viewPos - FragPos);
        vec3 reflectDir = reflect(-lightDir, norm);
        float spec = pow(max(dot(viewDir, reflectDir), 0.0), material.shininess);
        vec3 specular = sl.lightColor * (spec * material.specular);

        return (ambient + diffuse + specular) * attenuation;
    } else {
        // Outside Cone
        return sl.lightColor * material.ambient * attenuation;
    }
}

void main() {
    // 1. Calculate Lights Phong Shading
    vec3 lightingResult = vec3(0.0);
    lightingResult += calculateDirectionLight();
    lightingResult += calculatePointLight();
    lightingResult += calculateSpotLight();

    // 2. Sample Object Texture Diffuse Color
    vec4 texColor = texture(ourTexture, TexCoord);
    if(texColor.a < 0.1) texColor = vec4(1.0, 1.0, 1.0, 1.0);
    
    // 3. Calculate Reflection
    // Incident vector = Vector from Camera to Fragment
    vec3 I = normalize(FragPos - viewPos); 
    // Reflect that vector off the Normal
    vec3 R = reflect(I, normalize(Normal));
    // Sample the skybox using the reflection vector
    vec4 reflectionColor = texture(skybox, R);

    // 4. Mix Lighting and Reflection
    // Base appearance (Lighting * Texture)
    vec3 baseColor = lightingResult * texColor.rgb;
    
    // Mix base color with reflection based on reflectivity factor
    vec3 finalColor = mix(baseColor, reflectionColor.rgb, material.reflectivity);

    color = vec4(finalColor, 1.0);
}
