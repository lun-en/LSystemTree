#version 430

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texCoord;

uniform mat4 Projection;
uniform mat4 ViewMatrix;
uniform mat4 ModelMatrix;
uniform mat4 TIModelMatrix;

out vec2 TexCoord;
// Normal of vertex in world space
out vec3 Normal;
// Position of vertex in world space
out vec3 FragPos;

// TODO#3-3: vertex shader
// Note:
//           1. Outputs: pass any varyings needed by the fragment shader.
//           2. Transforms: transform positions with ModelMatrix and normals with the
//              transpose-inverse of the model.

void main() {
  TexCoord = texCoord;
  // Normal Matrix transformation
  Normal = mat3(TIModelMatrix) * normal;
  // Fragment Position in World Space
  FragPos = vec3(ModelMatrix * vec4(position, 1.0));
  
  // positions the vertex on screen
  gl_Position = Projection * ViewMatrix * ModelMatrix * vec4(position, 1.0);
}