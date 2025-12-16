//TreeGen.h
#pragma once
#include <vector>
#include <glm/glm.hpp>

struct VertexPN {
    glm::vec3 pos;
    glm::vec3 normal;
};

struct TreeParams {
    int   iterations = 4;

    float baseRadius = 0.3f;
    float baseLength = 1.5f;

    float radiusDecayF = 0.85f;
    float lengthDecayF = 0.95f;
    float branchRadiusDecay = 0.7f;

    float branchAngleDeg = 25.0f;

    int radialSegments = 12;

    bool addSpheres = true;
    int  sphereLatSegments = 6;
    int  sphereLonSegments = 8;

    glm::vec3 baseTranslation = glm::vec3(0.0f, -3.0f, 0.0f);
};

std::vector<VertexPN> BuildTreeVertices(const TreeParams& p);
