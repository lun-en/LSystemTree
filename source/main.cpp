//main.cpp
#include <iostream>
#include <vector>
#include <cstddef>
#include<string>
#include <filesystem>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "TreeGen.h"

namespace fs = std::filesystem;

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

static fs::path FindProjectRoot()
{
    fs::path p = fs::current_path();

    // Walk upward a few levels until we find /assets/textures
    for (int i = 0; i < 10; ++i) {
        if (fs::exists(p / "assets" / "textures")) return p;
        if (!p.has_parent_path()) break;
        p = p.parent_path();
    }
    return fs::current_path(); // fallback
}

static GLuint Make1x1TextureRGBA(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    unsigned char px[4] = { r, g, b, a };
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

// ---------------------------
// Hill mesh generation (Part 2)
// Uses VertexPN = { pos, normal, uv, tangent } like your tree.
// Generates a gentle mound + subtle noise, centered at (0, baseY, 0).
// ---------------------------
static float HillHeightFn(float x, float z, float baseY)
{
    // Broad mound (Gaussian-ish)
    float r2 = x * x + z * z;
    float moundHeight = 0.55f;
    float sigma = 10.0f; // bigger = wider hill
    float mound = moundHeight * std::exp(-r2 / (2.0f * sigma * sigma));

    // Subtle "old videogame" noise
    float noiseAmp = 0.10f;
    float n =
        0.60f * std::sin(0.35f * x + 0.15f * z) +
        0.40f * std::cos(0.25f * z - 0.10f * x) +
        0.25f * std::sin(0.18f * (x + z));

    return baseY + mound + noiseAmp * n;
}

static std::vector<VertexPN> BuildHillVertices(
    float baseY,
    float halfSize,
    int   gridN,
    float uvWorldU,
    float uvWorldV)
{
    gridN = std::max(4, gridN);
    uvWorldU = std::max(1e-6f, uvWorldU);
    uvWorldV = std::max(1e-6f, uvWorldV);

    const int N = gridN;
    const float size = 2.0f * halfSize;
    const float dx = size / float(N - 1);
    const float dz = size / float(N - 1);

    auto idx = [&](int i, int j) { return j * N + i; };

    std::vector<float> H(N * N, 0.0f);
    for (int j = 0; j < N; ++j) {
        float z = -halfSize + j * dz;
        for (int i = 0; i < N; ++i) {
            float x = -halfSize + i * dx;
            H[idx(i, j)] = HillHeightFn(x, z, baseY);
        }
    }

    std::vector<glm::vec3> P(N * N);
    std::vector<glm::vec3> Nrm(N * N);
    std::vector<glm::vec4> Tan(N * N);
    std::vector<glm::vec2> UV(N * N);

    for (int j = 0; j < N; ++j) {
        float z = -halfSize + j * dz;
        for (int i = 0; i < N; ++i) {
            float x = -halfSize + i * dx;

            float hC = H[idx(i, j)];
            int iL = std::max(0, i - 1), iR = std::min(N - 1, i + 1);
            int jD = std::max(0, j - 1), jU = std::min(N - 1, j + 1);

            float hL = H[idx(iL, j)];
            float hR = H[idx(iR, j)];
            float hD = H[idx(i, jD)];
            float hU = H[idx(i, jU)];

            float dhdx = (hR - hL) / (float(iR - iL) * dx);
            float dhdz = (hU - hD) / (float(jU - jD) * dz);

            glm::vec3 pos(x, hC, z);

            // Normal from heightfield gradients
            glm::vec3 n = glm::normalize(glm::vec3(-dhdx, 1.0f, -dhdz));

            // Tangent along +X direction (dP/dx)
            glm::vec3 t = glm::normalize(glm::vec3(1.0f, dhdx, 0.0f));

            // Bitangent along +Z direction (dP/dz)
            glm::vec3 b = glm::normalize(glm::vec3(0.0f, dhdz, 1.0f));

            // Orthonormalize tangent to normal and compute sign
            t = glm::normalize(t - n * glm::dot(n, t));
            float sign = (glm::dot(glm::cross(n, t), b) < 0.0f) ? -1.0f : 1.0f;

            // UV in world meters
            glm::vec2 uv(x / uvWorldU, z / uvWorldV);

            P[idx(i, j)]   = pos;
            Nrm[idx(i, j)] = n;
            Tan[idx(i, j)] = glm::vec4(t, sign);
            UV[idx(i, j)]  = uv;
        }
    }

    // Build triangle list (no EBO) so it matches your tree draw style
    std::vector<VertexPN> out;
    out.reserve((N - 1) * (N - 1) * 6);

    auto push = [&](int i, int j) {
        int k = idx(i, j);
        out.push_back({ P[k], Nrm[k], UV[k], Tan[k] });
    };

    for (int j = 0; j < N - 1; ++j) {
        for (int i = 0; i < N - 1; ++i) {
            // Quad corners: (i,j)=00, (i+1,j)=10, (i+1,j+1)=11, (i,j+1)=01
            push(i,     j);
            push(i + 1, j);
            push(i + 1, j + 1);

            push(i,     j);
            push(i + 1, j + 1);
            push(i,     j + 1);
        }
    }

    return out;
}

static GLuint LoadTexture2D(const fs::path& path, bool srgb)
{
    int w = 0, h = 0, comp = 0;
    stbi_set_flip_vertically_on_load(true);

    // Force 4 channels so OpenGL upload format is always consistent
    unsigned char* data = stbi_load(path.string().c_str(), &w, &h, &comp, 4);
    if (!data) {
        std::cerr << "Failed to load texture: " << path << "\n";
        return 0;
    }

    GLenum srcFormat = GL_RGBA;
    GLenum internalFormat = srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8;

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);

    // Safe even if widths are odd
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, w, h, 0, srcFormat, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(data);
    return tex;
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

    bool solidMode = false; // NEW: solid bark for screenshots
    bool envMode = false; // NEW: enable HDRI environment background (Part 1)

    bool DeciduousMode = true;
    
    // Iteration variables
    bool OWitFlag = false;
    int iterationCount = 1; // Default

    // Seed variables
    bool seedFlag = false;
    int seedValue = 2025;      // Default

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        // --- HELP LOGIC ---
        if (arg == "-h" || arg == "--help") {
            std::cout << "Usage: ./program.exe [options]\n\n"
                << "Options:\n"
                << "  -d, --deciduous     Set tree type to Deciduous (default)\n"
                << "  -c, --conifer       Set tree type to Conifer/Pine\n"
                << "  -s, --solid         Enable solid bark mode (for screenshots)\n"
                << "  -e, --environment   Enable HDRI background environment\n"
                << "  -i <number>         Set iteration count (default: 1)\n"
                << "  -seed <number>      Set generation seed (default: 2025)\n"
                << "  -h, --help          Show this help message\n\n"
                << "Examples:\n"
                << "  ./program.exe -c -i 12 -s\n"
                << "  ./program.exe -d -seed 12345\n";
            exit(0); // Stop program here
        }
        else if (arg == "--solid" || arg == "solid" || arg == "--flat" || arg == "flat" || arg == "-s") {
            solidMode = true;
        }
        else if (arg == "-e" || arg == "--environment") {
            envMode = true;
        }
        else if (arg == "deciduous" || arg == "--deciduous" || arg == "-d") {
            params.preset = TreePreset::Deciduous;
            DeciduousMode = true;
        }
        else if (arg == "conifer" || arg == "--conifer" || arg == "-c") {
            params.preset = TreePreset::Conifer;
            DeciduousMode = false;
        }
        // --- ITERATION LOGIC ---
        else if (arg == "-i") {
            if (i + 1 < argc) {
                i++; // Move to the number
                try {
                    int parsedVal = std::stoi(argv[i]);

                    // Catch negative numbers
                    if (parsedVal < 0) {
                        std::cout << "Warning: Iterations cannot be negative (" << parsedVal << "). Defaulting to 0.\n";
                        iterationCount = 0;
                    }
                    else {
                        iterationCount = parsedVal;
                    }
                    OWitFlag = true;
                }
                catch (...) {
                    std::cout << "Error: Invalid number provided for -i\n";
                }
            }
            else {
                std::cout << "Error: -i requires a number argument (e.g., -i 5).\n";
            }
        }
        // --- SEED LOGIC ---
        else if (arg == "-seed" || arg == "--seed") {
            if (i + 1 < argc) {
                i++; // Move to the number
                try {
                    seedValue = std::stoi(argv[i]);
                    seedFlag = true;
                    std::cout << "Seed set to: " << seedValue << "\n";
                }
                catch (...) {
                    std::cout << "Error: Invalid number provided for -seed\n";
                }
            }
            else {
                std::cout << "Error: -seed requires a number argument.\n";
            }
        }
        else {
            std::cout << "Unknown arg: " << arg
                << " (use: -h or --help to get help.)\n";
        }
    }

    if (solidMode) {
        std::cout << "SOLID MODE enabled (light gray bark, no texture detail)\n";
    }
    if (envMode) {
        std::cout << "ENVIRONMENT MODE enabled (HDRI background)\n";
    }

    fs::path root = FindProjectRoot();
    fs::path texRoot = root / "assets" / "textures";

    // ---------------------------
    // Ground textures (Part 2)
    // ---------------------------
    GLuint texGroundAlbedo = 0;
    GLuint texGroundNormal = 0;
    GLuint texGroundRough = 0;

    if (envMode) {
        fs::path groundRoot = root / "assets" / "ground";
        fs::path gset = (params.preset == TreePreset::Conifer)
            ? (groundRoot / "conifer")
            : (groundRoot / "deciduous");

        fs::path gDiff, gNor, gRough;

        if (params.preset == TreePreset::Conifer) {
            gDiff = gset / "forrest_ground_01_diff_1k.png";
            gNor = gset / "forrest_ground_01_nor_gl_1k.png";
            gRough = gset / "forrest_ground_01_rough_1k.png";
        }
        else {
            gDiff = gset / "red_laterite_soil_stones_diff_1k.png";
            gNor = gset / "red_laterite_soil_stones_nor_gl_1k.png";
            gRough = gset / "red_laterite_soil_stones_rough_1k.png";
        }

        std::cout << "Loading ground textures from:\n"
            << gDiff << "\n" << gNor << "\n" << gRough << "\n";

        texGroundAlbedo = LoadTexture2D(gDiff, true);
        texGroundNormal = LoadTexture2D(gNor, false);
        texGroundRough = LoadTexture2D(gRough, false);

        if (!texGroundAlbedo || !texGroundNormal || !texGroundRough) {
            std::cerr << "Warning: ground textures failed to load. Disabling env ground.\n";
            texGroundAlbedo = texGroundNormal = texGroundRough = 0;
        }
    }

    // ---------------------------
    // Environment HDRI (Part 1)
    // ---------------------------
    fs::path hdriPath;
    GLuint texHDRI = 0;

    if (envMode) {
        fs::path hdriRoot = root / "assets" / "HDRIs";
        if (params.preset == TreePreset::Conifer) {
            hdriPath = hdriRoot / "conifer" / "autumn_park_1k.png";
        }
        else {
            hdriPath = hdriRoot / "deciduous" / "belfast_sunset_1k.png";
        }

        std::cout << "Loading HDRI from:\n" << hdriPath << "\n";
        texHDRI = LoadTexture2D(hdriPath, true); // sRGB for PNG

        if (texHDRI) {
            // For lat-long env maps: wrap S, clamp T is usually best

            glBindTexture(GL_TEXTURE_2D, texHDRI);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glBindTexture(GL_TEXTURE_2D, 0);

        }
        else {
            std::cerr << "Warning: HDRI failed to load, environment will be disabled.\n";
            envMode = false;
        }
    }

    fs::path diffPath, norPath, roughPath;

    if (params.preset == TreePreset::Conifer) {
        fs::path p = texRoot / "pine_bark_1k.blend";
        diffPath = p / "pine_bark_diff_1k.png";
        norPath = p / "pine_bark_nor_gl_1k.png";
        roughPath = p / "pine_bark_rough_1k.png";
    }
    else {
        fs::path p = texRoot / "bark_brown_02_1k.blend";
        diffPath = p / "bark_brown_02_diff_1k.png";
        norPath = p / "bark_brown_02_nor_gl_1k.png";
        roughPath = p / "bark_brown_02_rough_1k.png";
    }

    GLuint texAlbedo = 0, texNormal = 0, texRough = 0;

    if (!solidMode) {
        std::cout << "Loading bark textures from:\n"
            << diffPath << "\n"
            << norPath << "\n"
            << roughPath << "\n";

        std::cout << "Loading albedo...\n";
        texAlbedo = LoadTexture2D(diffPath, true);

        std::cout << "Loading normal...\n";
        texNormal = LoadTexture2D(norPath, false);

        std::cout << "Loading roughness...\n";
        texRough = LoadTexture2D(roughPath, false);

        std::cout << "All textures loaded.\n";
    }
    else {
        // Solid-look “textures”:
        // - Albedo: white (so uBaseColor controls the final color)
        // - Normal: flat normal (no bumps)
        // - Roughness: constant mid/high roughness (less shiny)
        texAlbedo = Make1x1TextureRGBA(255, 255, 255, 255);
        texNormal = Make1x1TextureRGBA(128, 128, 255, 255);
        texRough = Make1x1TextureRGBA(200, 200, 200, 255);
    }

    params.iterations = 15;          // start lower to avoid twig explosion; bump to 15 if too sparse

    params.radialSegments = 12;

    //params.seed = 1166707377;
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

    params.angleJitterDeg = 17.0f;    // less random-looking noise

    params.lengthJitterFrac = 0.08f; // more consistent segment lengths
    params.radiusJitterFrac = 0.06f; // less �sparkly� thickness noise

    params.branchRollJitterDeg = 35.0f; // 90 makes distribution look chaotic; phyllotaxis already spreads 360�
    params.branchPitchMinDeg = 15.0f;
    params.branchPitchMaxDeg = 50.0f;   // better 3D crown without relying on huge roll jitter

    params.enableRadiusPruning = true;
    params.pruneRadius = 0.0020f;       // prune more of the ultra-fine structural recursion (reduces clutter)

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

    //new conifer params
    if (params.preset == TreePreset::Conifer) {
        // Keep your junctio/n spheres if you like the look (optional)
        params.addSpheres = true;

        // Spruce: enough iterations for tufting, without blowing up too hard
        params.iterations = 15;

        // Trunk / taper: avoid “everything shrinks linearly with height”
        params.baseRadius = 0.30f;
        params.baseLength = 1.5f;   // slightly shorter = more whorl nodes
        params.radiusDecayF = 0.955f;  // gentler continuous taper (big difference)
        params.lengthDecayF = 0.955f;  // trunk segments don’t shrink away quickly

        // Enable curved taper for trunk (keeps base sturdy, tapers more near the top)
        params.enableTrunkTaperCurve = true;
        params.trunkTaperPower = 1.35f;
        params.trunkTaperTopMult = 0.75f; //0.92

        //enable scaffold taper curve 
        params.enableScaffoldTaperCurve = true;


        // Branch scaling at '[' : THIS fixes “branches are too thin”
        params.branchRadiusDecay = 0.38f;  // (was 0.20!) big improvement to scaffold thickness
        params.branchLengthDecay = 0.60f;  // branches start shorter than trunk, but not tiny

        // Angles: smaller angle + grammar controls whorl tilt (spruce look)
        params.branchAngleDeg = 35.0f;
        params.angleJitterDeg = 5.0f;

        // Mild geometry noise (breaks symmetry without chaos)
        params.lengthJitterFrac = 0.05f;
        params.radiusJitterFrac = 0.02f;

        // Distribute branches around trunk
        params.usePhyllotaxisRoll = true;
        params.phyllotaxisDeg = 137.5f;
        params.branchRollJitterDeg = 10.0f;

        // Allow a small random pitch kick to break perfect tier symmetry
        params.branchPitchMinDeg = 3.0f;
        params.branchPitchMaxDeg = 10.0f;

        // Branch crowding controls
        params.maxBranchesPerNode = 115;
        params.minBranchSpacing = 1;

        // Optional skipping (creates gaps, reduces “uniform cone” feeling)
        params.enableBranchSkipping = false;
        params.branchSkipMaxProb = 0.15f;
        params.branchSkipStartDepth = 3;
        params.minRadiusForBranch = 0.010f;

        // IMPORTANT: keep trunk from entering “twig scaling” too early
        params.depthFullEffect = 40;

        // Tropism: for spruce, use slight downward bend (droop)
        params.enableTropism = true;
        params.tropismDir = glm::vec3(0, 1, 0);
        params.tropismStrength = 0.008f;
        params.tropismThinBoost = 0.25f;

        // Twigs: don’t over-shorten (old 0.6 made upper structure collapse)
        params.twigLengthBoost = 0.20f;
        params.maxLenToRadius = 14.0f;

        // Pruning / visibility (keeps tips from turning into hair)
        params.enableRadiusPruning = false;
        params.pruneRadius = 0.0015f;

        params.minRadius = 0.0012f;
        params.minLength = 0.012f;

        // Crookedness optional (leave off for now)
        params.enableCrookedness = false;
        params.crookStrength = 0.5f;
        params.crookAccelDeg = 20.2f;
        params.crookDamping = 0.10f;

        params.radialSegments = 8;

        params.enableCrookedness = true;
    }

    if (params.preset == TreePreset::Conifer) {
        params.barkRepeatWorldU = 1.10f;
        params.barkRepeatWorldV = 2.00f;
    }
    else { // Deciduous
        params.barkRepeatWorldU = 1.60f;
        params.barkRepeatWorldV = 2.60f;
    }

    if (OWitFlag) {
        params.iterations = iterationCount;
    }

    if (seedFlag) {
        params.seed = seedValue;
    }

    std::vector<VertexPN> verts;
    try {
        verts = BuildTreeVertices(params);
    }
    catch (const std::bad_alloc& e) {
        std::cerr << "Out of memory while building tree: " << e.what() << "\n";
        return -1;
    }
    catch (const std::exception& e) {
        std::cerr << "Exception while building tree: " << e.what() << "\n";
        return -1;
    }
    std::cout << "Tree vertices: " << verts.size() << "\n";

    // ---- Upload to GPU ----
    GLuint vao = 0, vbo = 0;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);

    // ---- Hill GPU handles (Part 2) ----
    GLuint hillVAO = 0, hillVBO = 0;
    GLsizei hillVertCount = 0;

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

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(VertexPN), (void*)offsetof(VertexPN, uv));

    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(VertexPN), (void*)offsetof(VertexPN, tangent));

    glBindVertexArray(0);

    // ---------------------------
// Hill mesh (Part 2) GPU upload
// ---------------------------
    if (envMode) {
        // NOTE: This assumes you already loaded:
        // GLuint texGroundAlbedo, texGroundNormal, texGroundRough;
        // and you already implemented BuildHillVertices(...) above main().

        // Put ground near the tree base (your tree vertices already include baseTranslation)
        float baseY = params.baseTranslation.y - 0.20f;

        // Size/resolution (tweak later)
        float halfSize = 30.0f;   // bigger plane, we’ll hide edges with mask
        int   gridN = 120;     // smoother mound

        // UVBigger numbers = fewer repeats (less tiling)
        float uvWorldU = (params.preset == TreePreset::Conifer) ? 12.0f : 14.0f;
        float uvWorldV = (params.preset == TreePreset::Conifer) ? 12.0f : 14.0f;

        std::vector<VertexPN> hillVerts = BuildHillVertices(baseY, halfSize, gridN, uvWorldU, uvWorldV);
        hillVertCount = (GLsizei)hillVerts.size();

        glGenVertexArrays(1, &hillVAO);
        glGenBuffers(1, &hillVBO);

        glBindVertexArray(hillVAO);
        glBindBuffer(GL_ARRAY_BUFFER, hillVBO);
        glBufferData(GL_ARRAY_BUFFER,
            (GLsizeiptr)(hillVerts.size() * sizeof(VertexPN)),
            hillVerts.data(),
            GL_STATIC_DRAW);

        // Same attribute layout as your tree:
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(VertexPN), (void*)offsetof(VertexPN, pos));

        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(VertexPN), (void*)offsetof(VertexPN, normal));

        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(VertexPN), (void*)offsetof(VertexPN, uv));

        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(VertexPN), (void*)offsetof(VertexPN, tangent));

        glBindVertexArray(0);
    }

    // ---- Shaders ----
    const char* vsSrc = R"GLSL(
        #version 330 core
        layout(location=0) in vec3 aPos;
        layout(location=1) in vec3 aNormal;
        layout(location=2) in vec2 aUV;
        layout(location=3) in vec4 aTangent; // xyz tangent, w sign
    
        uniform mat4 uModel;
        uniform mat4 uViewProj;
    
        out vec2 vUV;
        out vec3 vWorldPos;
        out vec3 vT;
        out vec3 vB;
        out vec3 vN;
    
        void main() {
            vec4 world = uModel * vec4(aPos, 1.0);
            vWorldPos = world.xyz;
    
            mat3 nmat = mat3(transpose(inverse(uModel)));
    
            vec3 N = normalize(nmat * aNormal);
            vec3 T = normalize(nmat * aTangent.xyz);
    
            // Orthonormalize T against N (stabilizes normal mapping)
            T = normalize(T - N * dot(N, T));
    
            vec3 B = cross(N, T) * aTangent.w;
    
            vN = N;
            vT = T;
            vB = B;
            vUV = aUV;
    
            gl_Position = uViewProj * world;
        }
    )GLSL";

    const char* fsSrc = R"GLSL(
        #version 330 core
        in vec2 vUV;
        in vec3 vWorldPos;
        in vec3 vT;
        in vec3 vB;
        in vec3 vN;
    
        uniform sampler2D uAlbedoTex;
        uniform sampler2D uNormalTex;
        uniform sampler2D uRoughTex;
    
        uniform vec3 uBaseColor;
        uniform vec3 uLightDir;
        uniform vec3 uAmbient;
        uniform vec3 uCamPos;
    
        uniform float uNormalStrength;  // 0..2
        uniform float uSpecPower;       // e.g. 32
        uniform float uSpecStrength;    // 0..1
        uniform bool  uFlipNormalY;     // set true only if bumps look inverted

        uniform float uMacroFreq;       // e.g. 0.12
        uniform float uMacroStrength;   // e.g. 0.20
        uniform float uUVWarp;          // e.g. 0.02
        uniform float uBarkTwist;       // e.g. 0.08 (optional)

        uniform bool  uUseAltTiling;     // ground: ON, tree: OFF
        uniform float uAltTilingMix;     // 0..1
        uniform bool  uUseGroundMask;    // ground: ON, tree: OFF
        uniform float uGroundRadius;     // world units
        uniform float uGroundFade;       // world units
        uniform float uGroundCutoff;     // 0 = disabled, >0 = discard alpha below cutoff (for depth prepass)   
        
        out vec4 FragColor;

        float hash31(vec3 p) {
            return fract(sin(dot(p, vec3(127.1, 311.7, 74.7))) * 43758.5453123);
        }
        
        float noise3(vec3 p) {
            vec3 i = floor(p);
            vec3 f = fract(p);
            f = f*f*(3.0 - 2.0*f);
        
            float n000 = hash31(i + vec3(0,0,0));
            float n100 = hash31(i + vec3(1,0,0));
            float n010 = hash31(i + vec3(0,1,0));
            float n110 = hash31(i + vec3(1,1,0));
            float n001 = hash31(i + vec3(0,0,1));
            float n101 = hash31(i + vec3(1,0,1));
            float n011 = hash31(i + vec3(0,1,1));
            float n111 = hash31(i + vec3(1,1,1));
        
            float nx00 = mix(n000, n100, f.x);
            float nx10 = mix(n010, n110, f.x);
            float nx01 = mix(n001, n101, f.x);
            float nx11 = mix(n011, n111, f.x);
        
            float nxy0 = mix(nx00, nx10, f.y);
            float nxy1 = mix(nx01, nx11, f.y);
        
            return mix(nxy0, nxy1, f.z);
        }
    
        void main() {
        
            vec2 uv = vUV;
        
            // optional subtle spiral twist (tree bark)
            uv.x += uv.y * uBarkTwist;
        
            // macro noise from world pos (already in your shader)
            float m  = noise3(vWorldPos * uMacroFreq);
            float m2 = noise3((vWorldPos + vec3(17.0, 5.0, 11.0)) * uMacroFreq);
            vec2 warp = vec2(m, m2) - 0.5;
            uv += warp * uUVWarp;
        
            // --- Anti-tiling second sample (use for ground) ---
            vec2 uv2 = uv;
            float blend = 0.0;
            if (uUseAltTiling) {
                float a = 0.73; // radians (~42 deg)
                mat2 R = mat2(cos(a), -sin(a),
                              sin(a),  cos(a));
                uv2 = R * (uv * 1.37 + vec2(0.123, 0.456));
        
                // Stable blend mask from world-space noise
                blend = smoothstep(0.25, 0.75, noise3(vWorldPos * 0.20));
                blend *= clamp(uAltTilingMix, 0.0, 1.0);
            }
        
            // Sample textures (blend two UV sets to break regular repeats)
            vec3 alb1 = texture(uAlbedoTex, uv).rgb;
            vec3 alb2 = texture(uAlbedoTex, uv2).rgb;
            vec3 albedo = mix(alb1, alb2, blend) * uBaseColor;
        
            float rough1 = texture(uRoughTex, uv).r;
            float rough2 = texture(uRoughTex, uv2).r;
            float rough = mix(rough1, rough2, blend);
        
            vec3 n1 = texture(uNormalTex, uv).xyz  * 2.0 - 1.0;
            vec3 n2 = texture(uNormalTex, uv2).xyz * 2.0 - 1.0;
            if (uFlipNormalY) { n1.y = -n1.y; n2.y = -n2.y; }
        
            vec3 nTS = normalize(mix(n1, n2, blend));
            nTS.xy *= uNormalStrength;
            nTS = normalize(nTS);
        
            mat3 TBN = mat3(normalize(vT), normalize(vB), normalize(vN));
            vec3 N = normalize(TBN * nTS);
        
            vec3 L = normalize(uLightDir);
            float diff = max(dot(N, L), 0.0);
        
            vec3 V = normalize(uCamPos - vWorldPos);
            vec3 H = normalize(L + V);
        
            // Existing macro modulation
            float macro = mix(1.0 - uMacroStrength, 1.0 + uMacroStrength, m);
            albedo *= macro;
            rough = clamp(rough + (m - 0.5) * 0.35 * uMacroStrength, 0.0, 1.0);
        
            float spec = pow(max(dot(N, H), 0.0), uSpecPower);
            spec *= uSpecStrength * (1.0 - rough);
        
            vec3 col = albedo * (uAmbient + diff) + vec3(spec);
        
            // --- Circular ground mask (hide square plane edges) ---
            float alpha = 1.0;
            if (uUseGroundMask) {
                float d = length(vWorldPos.xz); // center at origin
                alpha = 1.0 - smoothstep(uGroundRadius, uGroundRadius + uGroundFade, d);
                alpha = clamp(alpha, 0.0, 1.0);
            
                // Depth prepass uses this to keep only the opaque center
                if (uGroundCutoff > 0.0 && alpha < uGroundCutoff)
                    discard;
            }
            
            // STRAIGHT alpha output (no premultiply) for standard blending
            FragColor = vec4(col, alpha);
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

    GLint uAlbedoTexLoc = glGetUniformLocation(prog, "uAlbedoTex");
    GLint uNormalTexLoc = glGetUniformLocation(prog, "uNormalTex");
    GLint uRoughTexLoc = glGetUniformLocation(prog, "uRoughTex");

    GLint uBaseColorLoc = glGetUniformLocation(prog, "uBaseColor");
    GLint uCamPosLoc = glGetUniformLocation(prog, "uCamPos");

    GLint uNormalStrLoc = glGetUniformLocation(prog, "uNormalStrength");
    GLint uSpecPowerLoc = glGetUniformLocation(prog, "uSpecPower");
    GLint uSpecStrLoc = glGetUniformLocation(prog, "uSpecStrength");
    GLint uFlipNormalYLoc = glGetUniformLocation(prog, "uFlipNormalY");

    glUseProgram(prog);
    glUniform1i(uAlbedoTexLoc, 0);
    glUniform1i(uNormalTexLoc, 1);
    glUniform1i(uRoughTexLoc, 2);

    // ---------------------------
    // Sky background (HDRI) (Part 1)
    // ---------------------------
    GLuint skyProg = 0;
    GLuint skyVAO = 0;

    GLint uSkyTexLoc = -1;
    GLint uSkyInvProjLoc = -1;
    GLint uSkyInvViewRotLoc = -1;
    GLint uSkyWorldRotLoc = -1;
    GLint uSkyResLoc = -1;
    GLint uSkyExposureLoc = -1;
    GLint uSkyGammaLoc = -1;
    GLint uSkyFlipVLoc = -1;

    if (envMode && texHDRI) {
        const char* skyVsSrc = R"GLSL(
        #version 330 core
        void main() {
            vec2 pos;
            if (gl_VertexID == 0) pos = vec2(-1.0, -1.0);
            else if (gl_VertexID == 1) pos = vec2( 3.0, -1.0);
            else pos = vec2(-1.0,  3.0);
            gl_Position = vec4(pos, 0.0, 1.0);
        }
    )GLSL";

        const char* skyFsSrc = R"GLSL(
        #version 330 core
        out vec4 FragColor;

        uniform sampler2D uHDRI;
        uniform mat4  uInvProj;
        uniform mat3  uInvViewRot;
        uniform mat3  uWorldRot;
        uniform vec2  uResolution;

        uniform float uExposure;
        uniform float uGamma;
        uniform bool  uFlipV;

        const float PI = 3.14159265359;

        vec2 DirToEquirectUV(vec3 d) {
            d = normalize(d);
            float phi   = atan(d.z, d.x);                 // -PI..PI
            float theta = asin(clamp(d.y, -1.0, 1.0));    // -PI/2..PI/2
            vec2 uv;
            uv.x = phi / (2.0 * PI) + 0.5;
            uv.y = theta / PI + 0.5;
            return uv;
        }

        void main() {
            vec2 uv  = gl_FragCoord.xy / uResolution;
            vec2 ndc = uv * 2.0 - 1.0;

            // Reconstruct view-space ray
            vec4 clip = vec4(ndc, 1.0, 1.0);
            vec4 view = uInvProj * clip;
            vec3 dirVS = normalize(view.xyz / max(view.w, 1e-6));

            // To world direction (camera rotation only)
            vec3 dirWS = normalize(uInvViewRot * dirVS);

            // Rotate environment with the tree/world
            dirWS = normalize(transpose(uWorldRot) * dirWS); // inverse for pure rotation matrices

            vec2 envUV = DirToEquirectUV(dirWS);
            envUV.x = fract(envUV.x + 1e-4);              // wrap cleanly
            envUV.y = clamp(envUV.y, 1e-4, 1.0 - 1e-4);   // avoid pole edge
            
            if (uFlipV) envUV.y = 1.0 - envUV.y;

            vec3 col = texture(uHDRI, envUV).rgb;

            // Make LDR PNG feel less dull
            col *= uExposure;
            col = col / (col + vec3(1.0));           // mild Reinhard
            col = pow(col, vec3(1.0 / uGamma));      // gamma

            FragColor = vec4(col, 1.0);
        }
    )GLSL";

        GLuint svs = CompileShader(GL_VERTEX_SHADER, skyVsSrc);
        GLuint sfs = CompileShader(GL_FRAGMENT_SHADER, skyFsSrc);
        skyProg = LinkProgram(svs, sfs);
        glDeleteShader(svs);
        glDeleteShader(sfs);

        uSkyTexLoc = glGetUniformLocation(skyProg, "uHDRI");
        uSkyInvProjLoc = glGetUniformLocation(skyProg, "uInvProj");
        uSkyInvViewRotLoc = glGetUniformLocation(skyProg, "uInvViewRot");
        uSkyWorldRotLoc = glGetUniformLocation(skyProg, "uWorldRot");
        uSkyResLoc = glGetUniformLocation(skyProg, "uResolution");
        uSkyExposureLoc = glGetUniformLocation(skyProg, "uExposure");
        uSkyGammaLoc = glGetUniformLocation(skyProg, "uGamma");
        uSkyFlipVLoc = glGetUniformLocation(skyProg, "uFlipV");

        glGenVertexArrays(1, &skyVAO);

        glUseProgram(skyProg);
        glUniform1i(uSkyTexLoc, 3); // HDRI bound on texture unit 3
        glUseProgram(0);
    }

    glm::vec3 camPos(0.0f, 8.0f, 32.0f);
    glm::vec3 camTarget(0.0f, 8.0f, 0.0f);

    //if (DeciduousMode) {glm::vec3 camPos(0.0f, 10.0f, 20.0f); glm::vec3 camTarget(0.0f, 5.0f, 0.0f);}
    //else { glm::vec3 camPos(0.0f, 15.0f, 25.0f); glm::vec3 camTarget(0.0f, 7.5f, 0.0f); }

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

        // --- Sky pass (HDRI) ---
        if (envMode && texHDRI && skyProg && skyVAO) {
            glm::mat4 invProj = glm::inverse(proj);
            glm::mat3 invViewRot = glm::transpose(glm::mat3(view)); // inverse of view rotation
            glm::mat3 worldRot = glm::mat3(model);                // same rotation as the tree

            glDepthMask(GL_FALSE);
            glDisable(GL_DEPTH_TEST);

            glUseProgram(skyProg);
            glUniformMatrix4fv(uSkyInvProjLoc, 1, GL_FALSE, &invProj[0][0]);
            glUniformMatrix3fv(uSkyInvViewRotLoc, 1, GL_FALSE, &invViewRot[0][0]);
            glUniformMatrix3fv(uSkyWorldRotLoc, 1, GL_FALSE, &worldRot[0][0]);
            glUniform2f(uSkyResLoc, (float)gWidth, (float)gHeight);

            // Tune these later; just start here
            float exposure = (params.preset == TreePreset::Conifer) ? 1.25f : 1.45f;
            glUniform1f(uSkyExposureLoc, exposure);
            glUniform1f(uSkyGammaLoc, 2.2f);

            // You said you already flipped it correctly, so keep this OFF by default.
            // Turn to 1 if it becomes inverted again.
            glUniform1i(uSkyFlipVLoc, 0);

            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, texHDRI);

            glBindVertexArray(skyVAO);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glBindVertexArray(0);

            glBindTexture(GL_TEXTURE_2D, 0);
            glUseProgram(0);

            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_TRUE);
        }

        // ---------------------------
        // Draw hill (Part 2): two-pass
        //   Pass A: depth-only cutout (clips tree)
        //   Pass B: blended color (soft edge), no depth writes
        // ---------------------------
        if (envMode && hillVAO && hillVertCount > 0) {

            glUseProgram(prog);

            // Rotate hill with tree so environment matches
            glm::mat4 hillModel = model;
            glUniformMatrix4fv(uModelLoc, 1, GL_FALSE, &hillModel[0][0]);
            glUniformMatrix4fv(uViewProjLoc, 1, GL_FALSE, &viewProj[0][0]);

            // Bind ground textures
            glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, texGroundAlbedo);
            glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, texGroundNormal);
            glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, texGroundRough);

            // Material tuning for ground
            glUniform3f(uBaseColorLoc, 1.0f, 1.0f, 1.0f);
            glUniform3f(uCamPosLoc, camPos.x, camPos.y, camPos.z);

            glUniform1f(uNormalStrLoc, 1.0f);
            glUniform1f(uSpecPowerLoc, 48.0f);
            glUniform1f(uSpecStrLoc, 0.12f);
            glUniform1i(uFlipNormalYLoc, 0);

            // Subtle ground noise
            glUniform1f(glGetUniformLocation(prog, "uMacroFreq"), 0.03f);
            glUniform1f(glGetUniformLocation(prog, "uMacroStrength"), 0.18f);
            glUniform1f(glGetUniformLocation(prog, "uUVWarp"), 0.02f);
            glUniform1f(glGetUniformLocation(prog, "uBarkTwist"), 0.0f);

            // Circular mask params (shared by both passes)
            GLint locUseGroundMask = glGetUniformLocation(prog, "uUseGroundMask");
            GLint locGroundRadius = glGetUniformLocation(prog, "uGroundRadius");
            GLint locGroundFade = glGetUniformLocation(prog, "uGroundFade");
            GLint locGroundCutoff = glGetUniformLocation(prog, "uGroundCutoff");

            glUniform1i(locUseGroundMask, 1);
            glUniform1f(locGroundRadius, 14.0f);
            glUniform1f(locGroundFade, 6.0f);

            // Anti-tiling (ground only)
            glUniform1i(glGetUniformLocation(prog, "uUseAltTiling"), 1);
            glUniform1f(glGetUniformLocation(prog, "uAltTilingMix"), 0.75f);

            glBindVertexArray(hillVAO);

            // ---- Pass A: depth-only prepass (alpha cutout) ----
            glDisable(GL_BLEND);
            glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
            glDepthMask(GL_TRUE);
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LESS);

            glUniform1f(locGroundCutoff, 0.99f); // keep only opaque center in depth
            glDrawArrays(GL_TRIANGLES, 0, hillVertCount);

            // ---- Pass B: color pass (blended fade), no depth writes ----
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glDepthMask(GL_FALSE);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthFunc(GL_LEQUAL); // allow drawing exactly on prepass depth

            glUniform1f(locGroundCutoff, 0.0f); // disable discard; draw full fade
            glDrawArrays(GL_TRIANGLES, 0, hillVertCount);

            // Restore defaults for the tree
            glDepthFunc(GL_LESS);
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);

            glBindVertexArray(0);
        }

        glUseProgram(prog);
        glUniformMatrix4fv(uModelLoc, 1, GL_FALSE, &model[0][0]);
        glUniformMatrix4fv(uViewProjLoc, 1, GL_FALSE, &viewProj[0][0]);

        // Bind textures
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texAlbedo);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, texNormal);

        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, texRough);

        // Material params
        //glUniform3f(uBaseColorLoc, 0.55f, 0.27f, 0.07f);
        if (solidMode) glUniform3f(uBaseColorLoc, 0.75f, 0.75f, 0.75f); // light gray
        else           glUniform3f(uBaseColorLoc, 1.0f, 1.0f, 1.0f);

        glUniform3f(uCamPosLoc, camPos.x, camPos.y, camPos.z);

        glUniform1f(glGetUniformLocation(prog, "uMacroFreq"), 0.12f);
        glUniform1f(glGetUniformLocation(prog, "uMacroStrength"), 0.20f);
        glUniform1f(glGetUniformLocation(prog, "uUVWarp"), 0.02f);
        glUniform1f(glGetUniformLocation(prog, "uBarkTwist"), 0.08f);

        glUniform1i(glGetUniformLocation(prog, "uUseGroundMask"), 0);
        glUniform1i(glGetUniformLocation(prog, "uUseAltTiling"), 0);
        glUniform1f(glGetUniformLocation(prog, "uAltTilingMix"), 0.0f);

        glUniform1f(uNormalStrLoc, 1.0f);
        glUniform1f(uSpecPowerLoc, 32.0f);
        glUniform1f(uSpecStrLoc, solidMode ? 0.15f : 0.35f);
        glUniform1i(uFlipNormalYLoc, 0); // if bumps look "inside out", change to 1

        //glUniform3f(uColorLoc, 0.55f, 0.27f, 0.07f);
        glUniform3f(uAmbientLoc, 0.75f, 0.75f, 0.75f);

        if (solidMode) glUniform3f(uAmbientLoc, 0.50f, 0.50f, 0.50f); // light gray
        else           glUniform3f(uAmbientLoc, 0.65f, 0.65f, 0.65f);

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

    if (skyProg) glDeleteProgram(skyProg);
    if (skyVAO)  glDeleteVertexArrays(1, &skyVAO);
    if (texHDRI) glDeleteTextures(1, &texHDRI);

    glDeleteTextures(1, &texAlbedo);
    glDeleteTextures(1, &texNormal);
    glDeleteTextures(1, &texRough);

    glfwTerminate();
    return 0;
}
