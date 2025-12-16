//TreeGen.h
#pragma once
#include <vector>
#include <cstdint> 
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

    // NEW: reproducibility + organic variation
    std::uint32_t seed = 1337;

    float angleJitterDeg = 10.0f;  // jitter applied to yaw/pitch/roll ops
    float lengthJitterFrac = 0.15f;  // +/- fraction per segment
    float radiusJitterFrac = 0.10f;  // +/- fraction per segment

    // NEW: distribute branch planes around trunk
    bool  usePhyllotaxisRoll = true;
    float phyllotaxisDeg = 137.5f; // golden-angle-ish divergence
    float branchRollJitterDeg = 20.0f;

    // NEW: termination thresholds (prevents tiny noisy twigs)
    float minRadius = 0.01f;
    float minLength = 0.05f;

    // Optional: keep for later (OFF by default)
    bool  enableTropism = false;
    glm::vec3 tropismDir = glm::vec3(0.0f, -1.0f, 0.0f);
    float tropismStrength = 0.02f; // radians-ish per segment (small)
};

std::vector<VertexPN> BuildTreeVertices(const TreeParams& p);
