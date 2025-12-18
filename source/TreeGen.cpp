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


    // 1) L-system (deciduous-ish, stochastic, 3D tokens)
    // Convention:
    //   X = bud (rewrites, not drawn)
    //   F = draw wood + advance
    //   + - = yaw
    //   & ^ = pitch
    //   \ / = roll
    LSystem lsys;
    lsys.setSeed(p.seed);
    lsys.setAxiom("X");

    /*
    // X = trunk bud (rarely terminates)
    lsys.addRule('X', "F[+A][-A]X", 1.20f);
    lsys.addRule('X', "F[+A]X", 0.80f);
    lsys.addRule('X', "F[-A]X", 0.80f);
    //lsys.addRule('X', "FFX", 0.55f);
    //lsys.addRule('X', "FX", 0.90f);   // keep growing trunk
    lsys.addRule('X', "", 0.01f);   // VERY rare termination

    // reduce pure leader-shoot tendency
    lsys.addRule('X', "FX", 0.35f);   // was 0.90
    lsys.addRule('X', "FFX", 0.20f);   // was 0.55

    // NEW: canopy handoff (NO X at the end)
    // this converts the trunk tip into a branching "crown" instead of a needle
    lsys.addRule('X', "F[+A][-A][&A][^A]A", 0.55f);
    */

    // X = trunk bud (builds trunk + major scaffolding branches)
    // Keep some trunk extension, but make "branching while extending" dominate.
    lsys.addRule('X', "F[+A][-A]X", 1.10f);
    lsys.addRule('X', "F[+A][-A][&A][^A]X", 0.85f);
    //lsys.addRule('X', "F[&Y][^Y]X", 0.33f);
    //lsys.addRule('X', "F[\\Y][/Y]X", 0.33f);
    lsys.addRule('X', "F[+Y][-Y][&Y][^Y]X", 0.33f); 
    lsys.addRule('X', "F[\\Y][/Y][&Y][^Y]X", 0.33f);
    lsys.addRule('X', "F[+Y][-Y]X", 0.33f);

    // Rare “pure leader” (keep small or you get the needle)
    lsys.addRule('X', "FX", 0.10f);
    lsys.addRule('X', "FFX", 0.05f);

    // --- CANOPY HANDOFF ---
    // Instead of continuing as X forever, convert into a crown bud C.
    // This still grows upward, but its growth is *branch-driven*.
    lsys.addRule('X', "FC", 0.55f);
    lsys.addRule('X', "F[+A][-A][&A][^A]C", 0.70f);

    // A = newborn branch bud (guaranteed to produce at least 1 segment)
    lsys.addRule('A', "FY", 1.0f);
    lsys.addRule('A', "F[+Y]Y", 0.10f);  // was 0.40
    lsys.addRule('A', "F[-Y]Y", 0.10f);  // was 0.40

    // Y = branch bud (make sub-branching MUCH more common, and reduce early “stub” deaths)
    lsys.addRule('Y', "FY", 1.00f);  // was 1.80 (too “stick-y”)
    lsys.addRule('Y', "F[+Y]Y", 0.70f);  // was 0.40
    lsys.addRule('Y', "F[-Y]Y", 0.70f);  // was 0.40
    lsys.addRule('Y', "F[+Y][-Y]Y", 0.70f);  // was 0.18  (this makes the “real twig” look happen often)
    lsys.addRule('Y', "FFY", 0.25f);  // keep some longer runs

    lsys.addRule('Y', "F[&Y][^Y]Y", 0.15f);
    lsys.addRule('Y', "F[\\Y][/Y]Y", 0.15f);   // adds 3D spread in the crown

    // C = crown bud (keeps growing, but "puffs" into a canopy instead of a needle)
    // Note: includes roll tokens so the crown fills 360 degrees over time.
    lsys.addRule('C', "F[+Y][-Y][&Y][^Y]C", 0.90f);
    lsys.addRule('C', "F[\\Y][/Y][&Y][^Y]C", 0.30f);
    lsys.addRule('C', "F[+Y][-Y]C", 0.55f);
    lsys.addRule('C', "F[\\Y][/Y]C", 0.35f);   // adds 3D spread in the crown
    lsys.addRule('C', "FY", 0.20f);   // sometimes just feed into Y


    std::string sentence = lsys.generate(p.iterations);

    // Print Stats
    std::cout << "seed=" << p.seed
        << " iter=" << p.iterations
        << " enableSkip=" << p.enableBranchSkipping
        << " sentenceLen=" << sentence.size()
        << "\n";

    size_t countF = 0, countX = 0, countY = 0, countBrack = 0;
    for (char c : sentence) {
        if (c == 'F') ++countF;
        else if (c == 'X') ++countX;
        else if (c == 'Y') ++countY;
        else if (c == '[') ++countBrack;
    }
    std::cout << "F=" << countF << " X=" << countX << " Y=" << countY
        << " [=" << countBrack << "\n";

    // 2) Turtle init
    TurtleState cur;
    cur.transform = glm::translate(glm::mat4(1.0f), p.baseTranslation);
    cur.radius = p.baseRadius;
    cur.length = p.baseLength;
    cur.depth = 0;
    cur.localDepth = 0;
    cur.branchesAtNode = 0;

    std::vector<TurtleState> stack;
    stack.reserve(2048);

    std::uint32_t branchIndex = 0;

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


    // Helper: optional tropism (kept OFF by default)
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
        // thin01=0 near trunk, thin01->1 for thin branches
        float angle = p.tropismStrength * (1.0f + p.tropismThinBoost * thin01);

        // rotate around the turtle's current position (so translation doesn't orbit around origin)
        glm::vec3 pos = glm::vec3(cur.transform[3]);
        glm::mat4 T = glm::translate(glm::mat4(1.0f), pos);
        glm::mat4 Ti = glm::translate(glm::mat4(1.0f), -pos);
        glm::mat4 R = glm::rotate(glm::mat4(1.0f), angle, axis);
        cur.transform = T * R * Ti * cur.transform;
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

    // 3) Interpret
    for (size_t i = 0; i < sentence.size(); ++i) {
        char c = sentence[i];
        switch (c) {
        case 'F': {
            // jittered segment
            float len = cur.length * jitterFrac(p.lengthJitterFrac);
            float rBottom = cur.radius * jitterFrac(p.radiusJitterFrac);
            float rTop = (cur.radius * p.radiusDecayF) * jitterFrac(p.radiusJitterFrac);

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

            // optional curvature
            applyTropism();
            break;

        }

        case 'X':
            // bud symbol: does not draw, just exists for rewriting
            break;

        case 'Y':
            // bud symbol: does not draw, just exists for rewriting
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

            // Per-node cap: don’t allow “spray” of many branches from the same spot
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

            // distribute branch planes around trunk  (MOVE THIS UP)
            if (p.usePhyllotaxisRoll) {
                float roll = p.phyllotaxisDeg * float(branchIndex++);
                roll += randRange(-p.branchRollJitterDeg, +p.branchRollJitterDeg);
                rotateLocal(glm::radians(roll), glm::vec3(0, 1, 0)); // roll around heading
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

    std::cout << "skippedBranches=" << skippedBranches << "\n";

    return verts;
}
