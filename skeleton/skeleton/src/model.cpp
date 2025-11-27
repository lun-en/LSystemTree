#include "model.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
#include<string>

#include <glm/vec3.hpp>

Model* Model::fromObjectFile(const char* obj_file) {
  Model* m = new Model();

  std::ifstream ObjFile(obj_file);

  if (!ObjFile.is_open()) {
    std::cout << "Can't open File !" << std::endl;
    return NULL;
  }
  /* TODO#1: Load model data from OBJ file
   *         You only need to handle v, vt, vn, f
   *         Other fields you can directly ignore
   *         Fill data into m->positions, m->texcoords m->normals and m->numVertex
   *         Data format:
   *           For positions and normals
   *         | 0    | 1    | 2    | 3    | 4    | 5    | 6    | 7    | 8    | 9    | 10   | 11   | ...
   *         | face 1                                                       | face 2               ...
   *         | v1x  | v1y  | v1z  | v2x  | v2y  | v2z  | v3x  | v3y  | v3z  | v1x  | v1y  | v1z  | ...
   *         | vn1x | vn1y | vn1z | vn1x | vn1y | vn1z | vn1x | vn1y | vn1z | vn1x | vn1y | vn1z | ...
   *           For texcoords
   *         | 0    | 1    | 2    | 3    | 4    | 5    | 6    | 7    | ...
   *         | face 1                                  | face 2        ...
   *         | v1x  | v1y  | v2x  | v2y  | v3x  | v3y  | v1x  | v1y  | ...
   * Note:
   *        OBJ File Format (https://en.wikipedia.org/wiki/Wavefront_.obj_file)
   *        Vertex per face = 3 or 4
   */
  // Temporary storage for raw OBJ data
  std::vector<glm::vec3> temp_positions;
  std::vector<glm::vec2> temp_texcoords;
  std::vector<glm::vec3> temp_normals;

  std::string line;
  while (std::getline(ObjFile, line)) {
    std::stringstream ss(line);
    std::string prefix;
    ss >> prefix;

    if (prefix == "v") {
      // Vertex Position
      glm::vec3 pos;
      ss >> pos.x >> pos.y >> pos.z;
      temp_positions.push_back(pos);
    } else if (prefix == "vt") {
      // Texture Coordinate
      glm::vec2 tex;
      ss >> tex.x >> tex.y;
      temp_texcoords.push_back(tex);
    } else if (prefix == "vn") {
      // Normal Vector
      glm::vec3 norm;
      ss >> norm.x >> norm.y >> norm.z;
      temp_normals.push_back(norm);
    } else if (prefix == "f") {
      // 1. Read all vertex strings in the line
      std::string vertexStr;
      std::vector<std::string> faceVerts;
      while (ss >> vertexStr) {
        faceVerts.push_back(vertexStr);
      }

      // 2. Triangulate using Triangle Fan method
      // A Quad (v0, v1, v2, v3) two triangles: (v0, v1, v2) and (v0, v2, v3)
      for (size_t i = 0; i < faceVerts.size() - 2; ++i) {
        // The triangle consists of the first vertex (0) and the current two (i+1, i+2)
        std::string triangleVerts[3] = {faceVerts[0], faceVerts[i + 1], faceVerts[i + 2]};

        // 3. Parse the 3 vertices of this new triangle
        for (int j = 0; j < 3; ++j) {
          std::stringstream vss(triangleVerts[j]);
          std::string segment;
          std::vector<std::string> indices;

          while (std::getline(vss, segment, '/')) {
            indices.push_back(segment);
          }

          // Convert indices to int 
          int posIdx = std::stoi(indices[0]) - 1;
          int texIdx = std::stoi(indices[1]) - 1;
          int normIdx = std::stoi(indices[2]) - 1;

          // Push Position
          glm::vec3 p = temp_positions[posIdx];
          m->positions.push_back(p.x);
          m->positions.push_back(p.y);
          m->positions.push_back(p.z);

          // Push Texture Coord
          glm::vec2 t = temp_texcoords[texIdx];
          m->texcoords.push_back(t.x);
          m->texcoords.push_back(t.y);

          // Push Normal
          glm::vec3 n = temp_normals[normIdx];
          m->normals.push_back(n.x);
          m->normals.push_back(n.y);
          m->normals.push_back(n.z);

          m->numVertex++;
        }
      }
    }
    
  }

  ObjFile.close();
  return m;
}
