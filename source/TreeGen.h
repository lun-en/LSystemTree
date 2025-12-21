//TreeGen.h
#pragma once
#include <vector>
#include <cstdint> 
#include <glm/glm.hpp>
#include <random>

struct VertexPN {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec4 tangent; // xyz = tangent, w = sign (usually +1 or -1)
};
//NEW: Tree presets
enum class TreePreset
{
    Deciduous,
    Conifer
};


struct TreeParams {
    //Adding the preset, add this not affect the rest of the pipeline
    TreePreset preset = TreePreset::Deciduous;


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
    std::uint32_t seed = std::random_device{}();

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

    // NEW: prevent branch bunching
    int   minBranchSpacing = 1;      // minimum number of F segments between branch-starts on the same path
    int   maxBranchesPerNode = 4;    // cap how many '[' we allow before the next F

    // Depth scaling (0..1 over this depth range)
    int   depthFullEffect = 10;    // after this depth, depth-bias is at full strength

    // Make branches pop into true 3D at each branch start (helps �oak canopy� later too)
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

    // NEW: hard cap to prevent �long noodles� when radius is tiny
    float maxLenToRadius = 12.0f;        // len <= maxLenToRadius * radius

    // NEW: pruning control (separate from draw cutoff)
    bool  enableRadiusPruning = false;  // start OFF to get twigs back
    float pruneRadius = 0.0006f;        // much smaller than minRadius

    // Crookedness
    bool  enableCrookedness = false;
    float crookStrength = 5.0f;   // overall intensity
    float crookAccelDeg = 5.6f;   // random accel per segment (degrees)
    float crookDamping = 0.01f;  // 0.85–0.95 (higher = smoother)

    //float crookStrength = 10.0f;   // overall intensity
    //float crookAccelDeg = 0.6f;   // random accel per segment (degrees)
    //float crookDamping = 0.90f;  // 0.85–0.95 (higher = smoother)

    // Trunk taper curve (only affects the main trunk: stack.empty() path)
    bool  enableTrunkTaperCurve = true;

    // 1.0 = roughly linear, >1 = slower early, faster late (recommended 2.0–3.0)
    float trunkTaperPower = 2.2f;

    // Multiplies the decay near the top to make it slightly faster than linear.
    // 1.0 = no extra speed-up, 0.90–0.98 = subtle speed-up
    float trunkTaperTopMult = 0.95f;

    // --- Bark texture mapping (world-space repeat size) ---
    // Smaller = more tiling (more detail). These are in *world units per texture repeat*.
    float barkRepeatWorldU = 0.50f; // around circumference
    float barkRepeatWorldV = 0.50f; // along branch/trunk length

    // If true, each new branch starts its bark V at 0 (nice consistent bark scale per-branch).
    bool  resetBarkVOnBranch = true;

};

std::vector<VertexPN> BuildTreeVertices(const TreeParams& p);
