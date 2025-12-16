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

    // --- Depth bias / termination controls ---
    bool  enableBranchSkipping = false;
    int   branchSkipStartDepth = 6;     // start skipping branches after this many drawn segments on a path
    float branchSkipMaxProb = 0.75f; // upper bound skip probability
    float minRadiusForBranch = 0.035f;

    // Depth scaling (0..1 over this depth range)
    int   depthFullEffect = 10;    // after this depth, depth-bias is at full strength

    // Make branches pop into true 3D at each branch start (helps “oak canopy” later too)
    float branchPitchMinDeg = 10.0f; // pitch down/up range depends on your axis; this creates Z spread
    float branchPitchMaxDeg = 35.0f;

    // Tropism (first attempt)
    bool  enableTropism = false;               // turn on now (you can toggle in main.cpp)
    glm::vec3 tropismDir = glm::vec3(0, -1, 0);  // gravity-ish
    float tropismStrength = 0.015f;             // keep SMALL at first
    float tropismThinBoost = 0.08f;              // how much thin branches bend more

    // NEW: child branches should get shorter faster than trunk segments
    float branchLengthDecay = 0.75f;     // applied when entering '[' (separate from lengthDecayF)

    // NEW: make deep twigs shrink faster (depth-based)
    float twigLengthBoost = 0.30f;       // 0..1, higher = shorter twigs at high depth

    // NEW: hard cap to prevent “long noodles” when radius is tiny
    float maxLenToRadius = 12.0f;        // len <= maxLenToRadius * radius

};

std::vector<VertexPN> BuildTreeVertices(const TreeParams& p);
