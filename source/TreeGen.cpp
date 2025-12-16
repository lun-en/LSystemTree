//TreeGen.cpp
#include "TreeGen.h"
#include "LSystem.h"

#include <cmath>
#include <vector>
#include <glm/gtc/matrix_transform.hpp>

struct TurtleState {
    glm::mat4 transform;
    float radius;
    float length;
    int   depth;
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

    // 1) L-system
    LSystem lsys;
    lsys.setAxiom("A");
    lsys.addRule('A', "F[+A][-A]");
    std::string sentence = lsys.generate(p.iterations);

    // 2) Turtle init
    TurtleState cur;
    cur.transform = glm::translate(glm::mat4(1.0f), p.baseTranslation);
    cur.radius = p.baseRadius;
    cur.length = p.baseLength;
    cur.depth = 0;

    std::vector<TurtleState> stack;
    stack.reserve(2048);

    // 3) Interpret
    for (char c : sentence) {
        switch (c) {
        case 'F':
        case 'A': {
            float rBottom = cur.radius;
            float rTop = cur.radius * p.radiusDecayF;
            float len = cur.length;

            if (rBottom > 0.01f && len > 0.05f) {
                if (p.addSpheres) {
                    appendSphere(verts, rBottom, cur.transform, p.sphereLatSegments, p.sphereLonSegments);
                }
                appendFrustumSegment(verts, len, rBottom, rTop, cur.transform, p.radialSegments);
            }

            cur.transform = cur.transform * glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, len, 0.0f));
            cur.radius = rTop;
            cur.length = cur.length * p.lengthDecayF;
            cur.depth += 1;
            break;
        }
        case '+':
            cur.transform = cur.transform * glm::rotate(glm::mat4(1.0f),
                glm::radians(p.branchAngleDeg), glm::vec3(0.0f, 0.0f, 1.0f));
            break;
        case '-':
            cur.transform = cur.transform * glm::rotate(glm::mat4(1.0f),
                glm::radians(-p.branchAngleDeg), glm::vec3(0.0f, 0.0f, 1.0f));
            break;
        case '[':
            stack.push_back(cur);
            cur.radius *= p.branchRadiusDecay;
            cur.length *= p.lengthDecayF;
            break;
        case ']':
            if (!stack.empty()) { cur = stack.back(); stack.pop_back(); }
            break;
        default:
            break;
        }
    }

    return verts;
}
