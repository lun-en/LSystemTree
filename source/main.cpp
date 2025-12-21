//main.cpp
#include <iostream>
#include <vector>
#include <cstddef>
#include<string>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "TreeGen.h"

static int gWidth = 800;
static int gHeight = 600;

static void ProcessInput(GLFWwindow* window) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);
}

static GLuint CompileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);

    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
        std::string log(len, '\0');
        glGetShaderInfoLog(s, len, nullptr, log.data());
        std::cerr << "Shader compile error:\n" << log << "\n";
    }
    return s;
}

static GLuint LinkProgram(GLuint vs, GLuint fs) {
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);

    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetProgramiv(p, GL_INFO_LOG_LENGTH, &len);
        std::string log(len, '\0');
        glGetProgramInfoLog(p, len, nullptr, log.data());
        std::cerr << "Program link error:\n" << log << "\n";
    }
    return p;
}

//here in the declaration added the params : (int argc, char** argv)
int main(int argc, char** argv) {
    if (!glfwInit()) {
        std::cerr << "glfwInit failed\n";
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(gWidth, gHeight, "L-System Tree", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window\n";
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to initialize GLAD\n";
        glfwTerminate();
        return -1;
    }

    glfwSetFramebufferSizeCallback(window, [](GLFWwindow*, int w, int h) {
        gWidth = (w > 0) ? w : 1;
        gHeight = (h > 0) ? h : 1;
        glViewport(0, 0, gWidth, gHeight);
        });

    glViewport(0, 0, gWidth, gHeight);
    glEnable(GL_DEPTH_TEST);

    // ---- Build tree geometry (CPU) ----
    TreeParams params;

    //NEW add the preset option
    //choose preset from CLI argument
    // Usage to make each tree:
    //   opengl-template.exe deciduous
    //   opengl-template.exe conifer
    //   opengl-template.exe pine
    //default preset for NOW
    params.preset = TreePreset::Deciduous;

    if (argc >= 2) {
        std::string arg = argv[1]; 
        if (arg == "deciduous") {
            params.preset = TreePreset::Deciduous;

        } else if (arg == "conifer" || arg == "pine") {
            params.preset = TreePreset::Conifer;

        } else {
            std::cout << "Unknown preset: " << arg
                    << " (use: deciduous | conifer | pine)\n";
        }
    }

    params.iterations = 15;          // start lower to avoid twig explosion; bump to 15 if too sparse

    params.seed = 1337;
    params.addSpheres = true;

    params.branchAngleDeg = 22.0f;

    params.usePhyllotaxisRoll = true;
    params.phyllotaxisDeg = 137.5f;

    params.branchPitchMaxDeg = 60.0f;   // was 45 (or 35 earlier)

    params.baseRadius = 0.55f;
    params.baseLength = 1.6f;

    params.enableBranchSkipping = false;
    params.branchSkipMaxProb = 0.25f; // keep it mild for now
    params.branchSkipStartDepth = 3;
    params.minRadiusForBranch = 0.040f;
    params.depthFullEffect = 10;


    params.enableTropism = true;
    params.tropismDir = glm::vec3(0, 1, 0);
    params.tropismStrength = 0.015f;
    params.tropismThinBoost = 0.18f;

    params.maxLenToRadius = 14.0f;   // fine

    params.minBranchSpacing = 1;   // IMPORTANT: 2 will kill most of your grammar's branches
    params.maxBranchesPerNode = 128;   // start generous

    params.branchRadiusDecay = 0.75f;   // optional but helps preserve twig thickness
    params.branchLengthDecay = 0.85f;   // longer sub-branches than 0.55
    params.twigLengthBoost = 0.15f;    // 0.30 shortens twigs a lot -> looks �fuzzy� and cramped

    params.angleJitterDeg = 7.0f;    // less random-looking noise

    params.lengthJitterFrac = 0.08f; // more consistent segment lengths
    params.radiusJitterFrac = 0.06f; // less �sparkly� thickness noise

    params.branchRollJitterDeg = 35.0f; // 90 makes distribution look chaotic; phyllotaxis already spreads 360�
    params.branchPitchMinDeg = 15.0f;
    params.branchPitchMaxDeg = 50.0f;   // better 3D crown without relying on huge roll jitter

    params.enableRadiusPruning = true;
    params.pruneRadius = 0.0010f;       // prune more of the ultra-fine structural recursion (reduces clutter)

    params.minRadius = 0.0016f;         // draw fewer micro-twigs
    params.minLength = 0.010f;          // avoid tiny �hair� segments

    params.enableCrookedness = true;

    // stronger than 1, but not insane
    params.crookStrength = 2.4f;

    // bigger noise = more zig-zag
    params.crookAccelDeg = 18.0f;

    // smoothing: 0.85–0.95 is the useful range
    params.crookDamping = 0.10f;

    params.enableTrunkTaperCurve = false;

    params.trunkTaperPower = 2.2f;

    params.trunkTaperTopMult = 0.95f;


    // ---- Preset-specific overrides for the CONIFER tree ----
if (params.preset == TreePreset::Conifer) {
    // Geometry: avoid spheres (too many polys + hides junction issues)
    params.addSpheres = true;

    // Conifers explode fast — start lower than deciduous
    params.iterations = 13;

    // Trunk / taper
    params.baseRadius = 0.38f;
    params.baseLength   = 1.35f;     // slightly shorter segments = more "nodes"
    params.radiusDecayF = 0.95f;     // taper
    params.lengthDecayF = 0.97f;    // slows length shrink => less "bunched"

    // Branch scaling when entering '['
    params.branchRadiusDecay = 0.20f;  // how fast branches get thinner ok
    params.branchLengthDecay = 0.60f;  // branches start shorter than trunk ok

    // Angle controls (used by + - & ^ \ /)
    params.branchAngleDeg = 80.0f;   // tighter, more pine-like 22.0 at angle 90 to the trunk
    params.angleJitterDeg = 10.0f;    // less chaotic than deciduous

    params.lengthJitterFrac = 0.00f; // more consistent segment lengths
    params.radiusJitterFrac = 0.00f; // less �sparkly� thickness noise

    // Use phyllotaxis to distribute branches around trunk
    params.usePhyllotaxisRoll = true;
    params.phyllotaxisDeg = 1.5f;
    params.branchRollJitterDeg = 12.0f; //bit more than 6 so won't look planar

    // IMPORTANT: your interpreter applies a random pitch kick at '['.
    // For conifers, we pitch explicitly using '&' in the grammar, so disable the random kick:
    params.branchPitchMinDeg = 0.0f;
    params.branchPitchMaxDeg = 0.0f;

    // Reduce branch crowding
    params.maxBranchesPerNode = 128;
    params.minBranchSpacing = 1;

    //Branch skipping 
    params.enableBranchSkipping = false;
    params.branchSkipMaxProb = 0.25f; // keep it mild for now
    params.branchSkipStartDepth = 3;
    params.minRadiusForBranch = 0.040f;
    params.depthFullEffect = 10;

    // Optional: mild gravity bend helps drooping tips
    params.enableTropism = true;
    params.tropismDir = glm::vec3(0, 1, 0);
    params.tropismStrength = 0.010f; //0.010f
    params.tropismThinBoost = 0.16f;

    // Keep twigs from becoming hair
    params.twigLengthBoost = 0.6f;
    params.maxLenToRadius = 16.0f; 

    params.enableTrunkTaperCurve = false;
    params.trunkTaperPower = 2.2f;
    params.trunkTaperTopMult = 0.95f;

    //pruning / visibility
    params.enableRadiusPruning = true;
    params.pruneRadius = 0.0018f;

    params.minRadius = 0.0010f;
    params.minLength = 0.010f;

    //Enable croockness
    params.enableCrookedness = false;

    // stronger than 1, but not insane
    params.crookStrength = 2.4f;

    // bigger noise = more zig-zag
    params.crookAccelDeg = 18.0f;

    // smoothing: 0.85–0.95 is the useful range
    params.crookDamping = 0.10f;

    // Slightly lower mesh resolution for performance (optional)
    params.radialSegments = 8;
}



    std::vector<VertexPN> verts = BuildTreeVertices(params);
    std::cout << "Tree vertices: " << verts.size() << "\n";

    // ---- Upload to GPU ----
    GLuint vao = 0, vbo = 0;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER,
        (GLsizeiptr)(verts.size() * sizeof(VertexPN)),
        verts.data(),
        GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(VertexPN), (void*)offsetof(VertexPN, pos));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(VertexPN), (void*)offsetof(VertexPN, normal));

    glBindVertexArray(0);

    // ---- Shaders ----
    const char* vsSrc = R"GLSL(
        #version 330 core
        layout(location=0) in vec3 aPos;
        layout(location=1) in vec3 aNormal;

        uniform mat4 uModel;
        uniform mat4 uViewProj;

        out vec3 vWorldNormal;

        void main() {
            vec4 world = uModel * vec4(aPos, 1.0);
            mat3 nmat = mat3(transpose(inverse(uModel)));
            vWorldNormal = normalize(nmat * aNormal);
            gl_Position = uViewProj * world;
        }
    )GLSL";

    const char* fsSrc = R"GLSL(
        #version 330 core
        in vec3 vWorldNormal;

        uniform vec3 uColor;
        uniform vec3 uLightDir;
        uniform vec3 uAmbient;

        out vec4 FragColor;

        void main() {
            vec3 N = normalize(vWorldNormal);
            vec3 L = normalize(uLightDir);
            float diff = max(dot(N, L), 0.0);
            vec3 col = uColor * (uAmbient + diff);
            FragColor = vec4(col, 1.0);
        }
    )GLSL";

    GLuint vs = CompileShader(GL_VERTEX_SHADER, vsSrc);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fsSrc);
    GLuint prog = LinkProgram(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint uModelLoc = glGetUniformLocation(prog, "uModel");
    GLint uViewProjLoc = glGetUniformLocation(prog, "uViewProj");
    GLint uColorLoc = glGetUniformLocation(prog, "uColor");
    GLint uLightDirLoc = glGetUniformLocation(prog, "uLightDir");
    GLint uAmbientLoc = glGetUniformLocation(prog, "uAmbient");

    glm::vec3 camPos(0.0f, 10.0f, 25.0f);
    glm::vec3 camTarget(0.0f, 5.0f, 0.0f);

    while (!glfwWindowShouldClose(window)) {
        ProcessInput(window);

        glClearColor(0.06f, 0.06f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        //rotate model constantly
        float t = (float)glfwGetTime();
        glm::mat4 model = glm::rotate(glm::mat4(1.0f), t * 0.25f, glm::vec3(0, 1, 0));

        glm::mat4 view = glm::lookAt(camPos, camTarget, glm::vec3(0, 1, 0));
        float aspect = (float)gWidth / (float)gHeight;
        glm::mat4 proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 200.0f);
        glm::mat4 viewProj = proj * view;

        glUseProgram(prog);
        glUniformMatrix4fv(uModelLoc, 1, GL_FALSE, &model[0][0]);
        glUniformMatrix4fv(uViewProjLoc, 1, GL_FALSE, &viewProj[0][0]);

        glUniform3f(uColorLoc, 0.55f, 0.27f, 0.07f);
        glUniform3f(uAmbientLoc, 0.25f, 0.25f, 0.25f);

        glm::vec3 lightDir = glm::normalize(glm::vec3(0.4f, 1.0f, 0.3f));
        glUniform3f(uLightDirLoc, lightDir.x, lightDir.y, lightDir.z);

        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)verts.size());
        glBindVertexArray(0);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glDeleteProgram(prog);
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);

    glfwTerminate();
    return 0;
}
