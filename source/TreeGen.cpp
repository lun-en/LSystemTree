//TreeGen.cpp
#include "TreeGen.h"
#include "LSystem.h"

#include <random>
#include <cstdint>
#include <algorithm>

#include <cmath>
#include <vector>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

struct TurtleState {
    glm::mat4 transform;
    float radius;
    float length;
    int   depth;        // global-ish segment count along current path
    int   localDepth;   // NEW: segments since the last '[' (branch start)
    int branchesAtNode;   // NEW
    
    // Crookedness state (bounded “wiggle” angles)
    float crookYaw = 0.0f;
    float crookPitch = 0.0f;
    float crookRoll = 0.0f;

    float crookYawPrev = 0.0f;
    float crookPitchPrev = 0.0f;
    float crookRollPrev = 0.0f;

};

static void appendFrustumSegment(std::vector<VertexPN>& out,
    float length,
    float radiusBottom,
    float radiusTop,
    const glm::mat4& transform,
    int radialSegments)
{
    const float TWO_PI = 6.28318530718f;
    const glm::mat3 normalMatrix = glm::mat3(transform);

    auto XformPos = [&](const glm::vec3& p) {
        glm::vec4 wp = transform * glm::vec4(p, 1.0f);
        return glm::vec3(wp);
    };

    for (int i = 0; i < radialSegments; ++i) {
        float t0 = static_cast<float>(i) / radialSegments;
        float t1 = static_cast<float>(i + 1) / radialSegments;

        float a0 = t0 * TWO_PI;
        float a1 = t1 * TWO_PI;

        glm::vec3 p0b(radiusBottom * std::cos(a0), 0.0f, radiusBottom * std::sin(a0));
        glm::vec3 p1b(radiusBottom * std::cos(a1), 0.0f, radiusBottom * std::sin(a1));
        glm::vec3 p0t(radiusTop * std::cos(a0), length, radiusTop * std::sin(a0));
        glm::vec3 p1t(radiusTop * std::cos(a1), length, radiusTop * std::sin(a1));

        float aMid = 0.5f * (a0 + a1);
        glm::vec3 localN(std::cos(aMid), 0.0f, std::sin(aMid));
        glm::vec3 worldN = glm::normalize(normalMatrix * localN);

        glm::vec3 tp0b = XformPos(p0b);
        glm::vec3 tp1b = XformPos(p1b);
        glm::vec3 tp0t = XformPos(p0t);
        glm::vec3 tp1t = XformPos(p1t);

        out.push_back({ tp0b, worldN });
        out.push_back({ tp0t, worldN });
        out.push_back({ tp1t, worldN });

        out.push_back({ tp0b, worldN });
        out.push_back({ tp1t, worldN });
        out.push_back({ tp1b, worldN });
    }
}

static void appendSphere(std::vector<VertexPN>& out,
    float radius,
    const glm::mat4& transform,
    int latSegments,
    int lonSegments)
{
    const float PI = 3.14159265359f;
    const float TWO_PI = 6.28318530718f;

    const glm::mat3 normalMatrix = glm::mat3(transform);

    auto push = [&](const glm::vec3& localPos, const glm::vec3& localN) {
        glm::vec4 wp4 = transform * glm::vec4(localPos, 1.0f);
        glm::vec3 wp = glm::vec3(wp4);
        glm::vec3 wn = glm::normalize(normalMatrix * localN);
        out.push_back({ wp, wn });
    };

    for (int lat = 0; lat < latSegments; ++lat) {
        float v0 = static_cast<float>(lat) / latSegments;
        float v1 = static_cast<float>(lat + 1) / latSegments;

        float phi0 = v0 * PI;
        float phi1 = v1 * PI;

        for (int lon = 0; lon < lonSegments; ++lon) {
            float u0 = static_cast<float>(lon) / lonSegments;
            float u1 = static_cast<float>(lon + 1) / lonSegments;

            float th0 = u0 * TWO_PI;
            float th1 = u1 * TWO_PI;

            glm::vec3 n00(std::sin(phi0) * std::cos(th0), std::cos(phi0), std::sin(phi0) * std::sin(th0));
            glm::vec3 n01(std::sin(phi0) * std::cos(th1), std::cos(phi0), std::sin(phi0) * std::sin(th1));
            glm::vec3 n10(std::sin(phi1) * std::cos(th0), std::cos(phi1), std::sin(phi1) * std::sin(th0));
            glm::vec3 n11(std::sin(phi1) * std::cos(th1), std::cos(phi1), std::sin(phi1) * std::sin(th1));

            glm::vec3 p00 = radius * n00;
            glm::vec3 p01 = radius * n01;
            glm::vec3 p10 = radius * n10;
            glm::vec3 p11 = radius * n11;

            push(p00, n00);
            push(p10, n10);
            push(p11, n11);

            push(p00, n00);
            push(p11, n11);
            push(p01, n01);
        }
    }
}

//NEW: helper function preset Grammar
static void SetupDeciduousGrammar(LSystem& lsys, const TreeParams& p)
{
    //set the deciduoud Tree grammar
    // setAxiom + addRule(...) + lo que sea necesario para el deciduous actual
    
    // 1) L-system (deciduous-ish, stochastic, 3D tokens)
    // Convention:
    //   X = bud (rewrites, not drawn)
    //   F = draw wood + advance
    //   + - = yaw
    //   & ^ = pitch
    //   \ / = roll
    lsys.setSeed(p.seed);
    lsys.setAxiom("K");

    // Lower-trunk staging: denser scaffold for first ~6 segments, then handoff to X
    // Also spreads 4 scaffolds across TWO heights (Fix B style)
    lsys.addRule('K', "FL", 1.0f);
    lsys.addRule('L', "F[-A][-A]F[-A][-A]F[-A][-A]F[-A][-A]M", 1.0f);
    lsys.addRule('M', "[-A]F[+A][-A]F[+A][-A]F[+A][-A]F[+A]N", 1.0f);         // 4 scaffolds, split across 2 nodes
    lsys.addRule('N', "F[+A][-A]F[+A][-A]O", 1.0f);         // same
    lsys.addRule('O', "[-A]F[+A][-A]F[+A]P", 1.0f);                   // lighter as we go up
    lsys.addRule('P', "F[-A][-A]Q", 1.0f);
    lsys.addRule('Q', "FX", 1.0f);

    // --- X: LOWER trunk bud (denser scaffold zone) ---
    // baseline: 2 scaffolds per node
    lsys.addRule('X', "F[+A][-A]X", 1.30f);

    // NEW: TWO NODES per rewrite -> more lower-trunk scaffolds without affecting the top
    lsys.addRule('X', "F[+A][-A]F[+A][-A]X", 0.95f);

    // occasional 3-scaffold node (still okay with maxBranchesPerNode=4)
    lsys.addRule('X', "F[+A][-A][|A]X", 0.70f);

    // a tiny bit of �plain trunk� so it�s not perfectly periodic
    lsys.addRule('X', "FX", 0.06f);
    lsys.addRule('X', "FFX", 0.03f);

    // IMPORTANT knob: keep X alive longer so the lower trunk stays branchy
    lsys.addRule('X', "FT", 0.15f);   // was effectively switching too soon

    // rare end
    //lsys.addRule('X', "", 0.005f);

    // --- T: UPPER trunk bud (keep your current look here) ---
    lsys.addRule('T', "F[+A][-A]T", 1.40f);
    lsys.addRule('T', "F[+A][-A][&A][^A]T", 0.80f);
    lsys.addRule('T', "FT", 0.10f);
    lsys.addRule('T', "FFT", 0.05f);

    // crown handoff (same idea as before, but from T instead of X)
    lsys.addRule('T', "FC", 0.12f);
    lsys.addRule('T', "F[+A][-A][&A][^A]C", 0.10f);

    //lsys.addRule('T', "", 0.005f);

    // --- A: big branch bud (more lateral structure early, still controlled) ---
    lsys.addRule('A', "FA", 0.50f);
    lsys.addRule('A', "F[+Y]FA", 0.55f);
    lsys.addRule('A', "F[-Y]FA", 0.55f);
    lsys.addRule('A', "F[+Y][-Y]FA", 0.22f);
    lsys.addRule('A', "FY", 0.18f);

    // ---- PRIMARY BRANCH (scaffold) ----
    // Mostly extends, sometimes emits secondary twigs.
    lsys.addRule('Y', "FY",                1.00f);  // extend 
    lsys.addRule('Y', "FFY",               0.50f);  // occasional longer run 0.35
    lsys.addRule('Y', "F[+b][-b]Y", 1.20f);  // twig 0.28
    lsys.addRule('Y', "F[+b]Y", 0.60f);  // twig
    lsys.addRule('Y', "F[-b]Y", 0.60f);  // small tuft 0.18
    lsys.addRule('Y', "FY", 0.30f);  // stop extending0.25
    lsys.addRule('Y', "F", 0.50f);  // die off (rare)0.10

    // ---- SECONDARY TWIGS ----
    lsys.addRule('b', "F[+b][-b]Y", 1.00f);
    lsys.addRule('b', "F[+b]b", 0.50f);
    lsys.addRule('b', "F[-b]Y", 0.50f);
    lsys.addRule('b', "F", 0.80f);

    // --- C: crown bud (adds �air gaps� via FC so it�s less bunched-up) ---
    lsys.addRule('C', "FC", 0.85f);  // spacing / structure without spawning new twigs every step
    lsys.addRule('C', "F[+Y][-Y]C", 0.45f);
    lsys.addRule('C', "F[\\Y][/Y]C", 0.25f);
    lsys.addRule('C', "FY", 0.18f);
    //lsys.addRule('C', "", 0.03f);
}

static void SetupConiferGrammar(LSystem& lsys, const TreeParams& p)
{
    // De momento puede quedar básico/placeholder.
    lsys.setSeed(p.seed);
    // Placeholder for now:
    lsys.setAxiom("F");
    lsys.addRule('F', "FF", 1.0f);
}



std::vector<VertexPN> BuildTreeVertices(const TreeParams& p)
{
    std::vector<VertexPN> verts;

    // RNG for interpreter-side jitter (separate from L-system RNG)
    std::mt19937 rng(p.seed);
    auto rand01 = [&]() -> float {
        std::uniform_real_distribution<float> d(0.0f, 1.0f);
        return d(rng);
    };
    auto randRange = [&](float a, float b) -> float {
        std::uniform_real_distribution<float> d(a, b);
        return d(rng);
    };
    auto jitterFrac = [&](float frac) -> float {
        // returns multiplier in [1-frac, 1+frac]
        return 1.0f + randRange(-frac, +frac);
    };
    // Depth scaling helper
    auto saturate = [](float x) { return glm::clamp(x, 0.0f, 1.0f); };

    auto depthT = [&](int depth) {
        return saturate(float(depth) / float(std::max(1, p.depthFullEffect)));
    };

    // Use depth to scale angles/jitter down as we get into twigs
    auto angleWithDepth = [&](float baseDeg, int depth) {
        float t = depthT(depth);
        float scaled = glm::mix(baseDeg, baseDeg * 0.60f, t); // twigs: smaller angles
        float jitter = glm::mix(p.angleJitterDeg, p.angleJitterDeg * 0.40f, t);
        return glm::radians(scaled + randRange(-jitter, +jitter));
    };


    //Instead of the whole decidious rule grammar we set the helper function
    LSystem lsys;
    if (p.preset == TreePreset::Deciduous)
        SetupDeciduousGrammar(lsys, p);
    else
        SetupConiferGrammar(lsys, p);
    

    std::string sentence = lsys.generate(p.iterations);

    // Print Stats
    std::cout << "seed=" << p.seed
        << " iter=" << p.iterations
        << " enableSkip=" << p.enableBranchSkipping
        << " sentenceLen=" << sentence.size()
        << "\n";

    size_t countF = 0, countX = 0, countY = 0, countC = 0, countT = 0, countBrack = 0;
    for (char c : sentence) {
        if (c == 'F') ++countF;
        else if (c == 'X') ++countX;
        else if (c == 'Y') ++countY;
        else if (c == 'C') ++countC;
        else if (c == 'T') ++countT;
        else if (c == '[') ++countBrack;
    }
    std::cout << "F=" << countF << " X=" << countX << " Y=" << countY
        << " C=" << countC << " T=" << countT << " [=" << countBrack << "\n";

    // 2) Turtle init
    TurtleState cur;
    cur.transform = glm::translate(glm::mat4(1.0f), p.baseTranslation);
    cur.radius = p.baseRadius;
    cur.length = p.baseLength;
    cur.depth = 0;
    cur.localDepth = 0;
    cur.branchesAtNode = 0;

    // crookedness
    cur.crookYaw = cur.crookPitch = cur.crookRoll = 0.0f;
    cur.crookYawPrev = cur.crookPitchPrev = cur.crookRollPrev = 0.0f;

    std::vector<TurtleState> stack;
    stack.reserve(2048);

    std::uint32_t branchIndex = 0;
    std::uint32_t trunkBranchIndex = 0;

    std::cout << "[TreeGen] BUILD MARKER: 2025-12-17 A\n";

    // Helper: apply a local-space rotation (post-multiply)
    //auto rotateLocal = [&](float radians, const glm::vec3& localAxis) {
    //    cur.transform = cur.transform * glm::rotate(glm::mat4(1.0f), radians, localAxis);
    //};

    auto rotateLocal = [&](float angle, const glm::vec3& localAxis) {
        // rotate around the turtle's LOCAL axis (converted to world axis)
        glm::vec3 pos = glm::vec3(cur.transform[3]);

        // turn the requested local axis into a world-space axis using current orientation
        glm::vec3 worldAxis = glm::normalize(glm::mat3(cur.transform) * localAxis);

        glm::mat4 T = glm::translate(glm::mat4(1.0f), pos);
        glm::mat4 Ti = glm::translate(glm::mat4(1.0f), -pos);
        glm::mat4 R = glm::rotate(glm::mat4(1.0f), angle, worldAxis);

        cur.transform = T * R * Ti * cur.transform;
    };

    // Helper: tropism
    auto applyTropism = [&]() {
        if (!p.enableTropism) return;

        glm::vec3 target = glm::normalize(p.tropismDir);
        if (glm::length(target) < 1e-6f) return;

        // Current heading is local +Y in world space
        glm::vec3 heading = glm::normalize(glm::vec3(cur.transform * glm::vec4(0, 1, 0, 0)));
        glm::vec3 axis = glm::cross(heading, target);
        float axisLen = glm::length(axis);
        if (axisLen < 1e-6f) return;
        axis /= axisLen;

        float thin01 = 1.0f - glm::clamp(cur.radius / std::max(1e-6f, p.baseRadius), 0.0f, 1.0f);
        float angle = p.tropismStrength * (1.0f + p.tropismThinBoost * thin01);

        glm::vec3 pos = glm::vec3(cur.transform[3]);
        glm::mat4 T = glm::translate(glm::mat4(1.0f), pos);
        glm::mat4 Ti = glm::translate(glm::mat4(1.0f), -pos);
        glm::mat4 R = glm::rotate(glm::mat4(1.0f), angle, axis);
        cur.transform = T * R * Ti * cur.transform;
    };

    // Helper: crookedness (bounded, mean-reverting “wiggle” -> oak-like zig-zag)
// Uses existing params:
//   p.crookStrength  : overall intensity multiplier
//   p.crookAccelDeg  : noise amplitude per segment (degrees)
//   p.crookDamping   : 0..1, higher = smoother / slower changes (try 0.85~0.95)
    auto applyCrookedness = [&]() {
        if (!p.enableCrookedness) return;

        // How much should thick vs thin branches be affected?
        // thick01=1 near trunk, ->0 on tiny twigs
        float thick01 = glm::clamp(cur.radius / std::max(1e-6f, p.baseRadius), 0.0f, 1.0f);

        // Reduce effect on tiny twigs so you don't get “hair noise”
        float twigScale = glm::mix(0.25f, 1.0f, thick01);

        // Final strength for this segment
        float strength = p.crookStrength * twigScale;

        // Random target noise each step (degrees -> radians)
        // (Roll is weaker; too much roll looks chaotic.)
        float nYaw = glm::radians(randRange(-p.crookAccelDeg, +p.crookAccelDeg));
        float nPitch = glm::radians(randRange(-p.crookAccelDeg, +p.crookAccelDeg));
        float nRoll = glm::radians(randRange(-p.crookAccelDeg, +p.crookAccelDeg)) * 0.35f;

        // Mean-reverting “wiggle” angles (bounded, no long-term drift)
        // crookDamping near 1 -> smooth/slow; lower -> sharper zig-zag
        cur.crookYaw = cur.crookYaw * p.crookDamping + nYaw;
        cur.crookPitch = cur.crookPitch * p.crookDamping + nPitch;
        cur.crookRoll = cur.crookRoll * p.crookDamping + nRoll;

        // Apply ONLY the incremental change this step (prevents accumulation/drift)
        float dYaw = cur.crookYaw - cur.crookYawPrev;
        float dPitch = cur.crookPitch - cur.crookPitchPrev;
        float dRoll = cur.crookRoll - cur.crookRollPrev;

        cur.crookYawPrev = cur.crookYaw;
        cur.crookPitchPrev = cur.crookPitch;
        cur.crookRollPrev = cur.crookRoll;

        // Apply around LOCAL axes (your rotateLocal already rotates about the turtle position)
        rotateLocal(-dYaw * strength, glm::vec3(0, 0, 1)); // yaw
        rotateLocal(-dPitch * strength, glm::vec3(1, 0, 0)); // pitch
        rotateLocal(-dRoll * strength, glm::vec3(0, 1, 0)); // roll (around heading)
    };

    // Skip forward until the ']' that closes the *current* branch (one pop).
    // Assumes we are inside at least one '[' (i.e., stack is not empty).
    auto pruneCurrentBranch = [&](size_t& i) {
        int nesting = 0;
        while (i + 1 < sentence.size()) {
            ++i;
            char cc = sentence[i];
            if (cc == '[') nesting++;
            else if (cc == ']') {
                if (nesting == 0) {
                    // This closes the branch we are currently in.
                    if (!stack.empty()) {
                        cur = stack.back();
                        stack.pop_back();
                    }
                    return;
                }
                nesting--;
            }
        }

        // If we run off the end, just clear stack as a safe fallback.
        if (!stack.empty()) {
            cur = stack.front();
            stack.clear();
        }
    };

    size_t skippedBranches = 0;
    size_t trunkBranchStarts = 0;
    size_t nonTrunkBranchStarts = 0;


    // 3) Interpret
    for (size_t i = 0; i < sentence.size(); ++i) {
        char c = sentence[i];
        switch (c) {
        case 'F': {
            // jittered segment
            float len = cur.length * jitterFrac(p.lengthJitterFrac);
            float rBottom = cur.radius * jitterFrac(p.radiusJitterFrac);
            // --- per-segment radius decay (curved for trunk, linear for branches) ---
            float radiusDecayThisStep = p.radiusDecayF;

            // "Main trunk" is when we are NOT inside any '[' ... ']'
            bool isTrunk = stack.empty();

            if (isTrunk && p.enableTrunkTaperCurve) {
                float baseR = std::max(1e-6f, p.baseRadius);

                // 1 near base, -> 0 as it gets thinner
                float thick01 = glm::clamp(cur.radius / baseR, 0.0f, 1.0f);

                // 0 at base -> 1 near the top
                float progress = 1.0f - thick01;

                // Curve: >1 means "slow early, faster late"
                float s = std::pow(progress, std::max(0.01f, p.trunkTaperPower));

                // Decay near base: closer to 1.0 (slower taper)
                float decayNearBase = glm::mix(1.0f, p.radiusDecayF, 0.25f);

                // Decay near top: slightly faster than linear (a bit smaller)
                float decayNearTop = p.radiusDecayF * glm::clamp(p.trunkTaperTopMult, 0.0f, 1.0f);

                radiusDecayThisStep = glm::mix(decayNearBase, decayNearTop, s);
            }

            // Apply decay + jitter
            float rTop = (cur.radius * radiusDecayThisStep) * jitterFrac(p.radiusJitterFrac);

            float t = depthT(cur.depth);

            float maxLen = p.maxLenToRadius * rBottom;

            // depth shortening stays the same
            len *= glm::mix(1.0f, 1.0f - p.twigLengthBoost, t);

            // enforce len/radius cap
            len = std::min(len, maxLen);

            // enforce a minimum length, but NEVER exceed the maxLen cap
            len = std::max(len, std::min(p.minLength, maxLen));

            // Optional hard prune (STRUCTURAL), separate from draw cutoff
            if (p.enableRadiusPruning && (rBottom <= p.pruneRadius)) {
                if (!stack.empty()) {
                    pruneCurrentBranch(i); // jump to matching ']' and pop
                    break;
                }
                else {
                    // trunk pruned: nothing meaningful left to draw
                    // just stop advancing trunk segments
                    break;
                }
            }

            // Draw cutoff (VISUAL) only
            bool draw = (rBottom > p.minRadius);

            if (draw) {
                if (p.addSpheres) {
                    appendSphere(verts, rBottom, cur.transform, p.sphereLatSegments, p.sphereLonSegments);
                }
                appendFrustumSegment(verts, len, rBottom, rTop, cur.transform, p.radialSegments);
            }

            // ALWAYS advance + decay, even if not drawing
            cur.transform = cur.transform * glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, len, 0.0f));
            cur.radius = rTop;
            cur.length = cur.length * p.lengthDecayF;
            cur.depth += 1;
            cur.localDepth += 1;
            cur.branchesAtNode = 0;

            // optional curvature (affects the NEXT segment direction)
            applyCrookedness();
            applyTropism();
            break;
        }

        case 'X':
        case 'Y':
        case 'T':
        case 'C':
        case 'L':
        case 'M':
        case 'N':
        case 'O':
        case 'P':
        case 'Q':
            break;


            // Yaw around local Z (matches your existing convention)
        case '+': {
            float a = angleWithDepth(p.branchAngleDeg, cur.depth);
            rotateLocal(+a, glm::vec3(0, 0, 1));
            break;
        }
        case '-': {
            float a = angleWithDepth(p.branchAngleDeg, cur.depth);
            rotateLocal(-a, glm::vec3(0, 0, 1));
            break;
        }
                // Pitch around local X
        case '&': {
            float a = angleWithDepth(p.branchAngleDeg, cur.depth);
            rotateLocal(+a, glm::vec3(1, 0, 0));
            break;
        }
        case '^': {
            float a = angleWithDepth(p.branchAngleDeg, cur.depth);
            rotateLocal(-a, glm::vec3(1, 0, 0));
            break;
        }
                // Roll around heading (local +Y)
        case '\\': {
            float a = angleWithDepth(p.branchAngleDeg, cur.depth);
            rotateLocal(+a, glm::vec3(0, 1, 0));
            break;
        }
        case '/': {
            float a = angleWithDepth(p.branchAngleDeg, cur.depth);
            rotateLocal(-a, glm::vec3(0, 1, 0));
            break;
        }

        case '|': {
            rotateLocal(glm::radians(180.0f), glm::vec3(0, 0, 1));
            break;
        }

        case '[': {
            bool skip = false;

            // Only enforce spacing for *sub-branches* (inside a branch), not trunk-level branching.
            if (!stack.empty() && cur.localDepth < p.minBranchSpacing)
                skip = true;

            // Per-node cap: don�t allow �spray� of many branches from the same spot
            if (cur.branchesAtNode >= p.maxBranchesPerNode) skip = true;

            if (p.enableBranchSkipping && !stack.empty()) {
                float t = 0.0f;
                if (cur.localDepth >= p.branchSkipStartDepth) {
                    t = glm::clamp(float(cur.localDepth - p.branchSkipStartDepth) / 4.0f, 0.0f, 1.0f);
                }

                float prob = t * p.branchSkipMaxProb;

                //if (cur.radius < p.minRadiusForBranch) {
                //    prob = std::max(prob, 0.60f);
                //}

                if (rand01() < prob) skip = true;
            }

            if (skip) {
                int nesting = 1;
                while (i + 1 < sentence.size() && nesting > 0) {
                    ++i;
                    if (sentence[i] == '[') nesting++;
                    else if (sentence[i] == ']') nesting--;
                }
                skippedBranches++;
                break;
            }

            if (stack.empty()) trunkBranchStarts++;
            else               nonTrunkBranchStarts++;

            const bool parentIsTrunk = stack.empty();

            // parent bookkeeping first
            cur.branchesAtNode += 1;
            const int parentBranchOrdinal = cur.branchesAtNode; // 1..N at this same node

            // normal branch handling:
            cur.branchesAtNode += 1;     // parent bookkeeping first
            stack.push_back(cur);        // store parent WITH updated bookkeeping

            // child branch starts fresh
            cur.localDepth = 0;
            cur.branchesAtNode = 0;
            cur.depth = 0;               // IMPORTANT: don't inherit trunk depth

            // branch thickness/length reduction when entering a branch
            cur.radius *= p.branchRadiusDecay;
            cur.length *= p.branchLengthDecay;   // IMPORTANT: use branchLengthDecay, not lengthDecayF

            // crookedness should start “fresh” per branch (prevents inheriting a strong kink)
            cur.crookYaw = cur.crookPitch = cur.crookRoll = 0.0f;
            cur.crookYawPrev = cur.crookPitchPrev = cur.crookRollPrev = 0.0f;

            // distribute branch planes around trunk  (MOVE THIS UP)
            /*if (p.usePhyllotaxisRoll) {
                float roll = p.phyllotaxisDeg * float(branchIndex++);
                roll += randRange(-p.branchRollJitterDeg, +p.branchRollJitterDeg);
                rotateLocal(glm::radians(roll), glm::vec3(0, 1, 0)); // roll around heading
            }*/

            // distribute branch planes around trunk
            if (p.usePhyllotaxisRoll) {

                float rollDeg = 0.0f;

                if (parentIsTrunk) {
                    // ---- TRUNK scaffolds: enforce even 360� distribution ----
                    // Try 12 or 16 bins. 12 is a good start.
                    const int   bins = 12;
                    const float binSize = 360.0f / float(bins);

                    // trunkBranchIndex must be a separate counter (uint32_t) you keep outside the loop
                    // (If you don�t have it yet, add: std::uint32_t trunkBranchIndex = 0; near branchIndex)
                    int bin = int(trunkBranchIndex % bins);

                    // Optional: spread multiple branches spawned at the exact same trunk node
                    // by offsetting within the bins a bit
                    float intra = (parentBranchOrdinal - 1) * (binSize * 0.25f); // mild

                    rollDeg = bin * binSize + intra;

                    // Small jitter only (big jitter = clumps)
                    rollDeg += randRange(-8.0f, +8.0f);

                    trunkBranchIndex++;

                }
                else {
                    // ---- non-trunk branches: keep your phyllotaxis/jitter behavior ----
                    rollDeg = p.phyllotaxisDeg * float(branchIndex++);
                    rollDeg += randRange(-p.branchRollJitterDeg, +p.branchRollJitterDeg);
                }

                rotateLocal(glm::radians(rollDeg), glm::vec3(0, 1, 0));
            }

            // NEW: pitch kick so branches spread in true 3D  (MOVE THIS DOWN)
            float pitch = randRange(p.branchPitchMinDeg, p.branchPitchMaxDeg);
            if (rand01() < 0.5f) pitch = -pitch;
            rotateLocal(glm::radians(pitch), glm::vec3(1, 0, 0));

            break;
        }

        case ']':
            if (!stack.empty()) {
                cur = stack.back();
                stack.pop_back();
            }
            break;

        default:
            break;
        }
    }

    std::cout << "trunkBranchStarts=" << trunkBranchStarts
        << " nonTrunkBranchStarts=" << nonTrunkBranchStarts << "\n";

    std::cout << "skippedBranches=" << skippedBranches << "\n";

    return verts;
}
