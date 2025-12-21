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

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--solid" || arg == "solid" || arg == "--flat" || arg == "flat" || arg == "-s") {
            solidMode = true;
        }
        else if (arg == "deciduous" || arg == "-d") {
            params.preset = TreePreset::Deciduous;
        }
        else if (arg == "conifer" || arg == "pine" || arg == "-c") {
            params.preset = TreePreset::Conifer;
        }
        else {
            std::cout << "Unknown arg: " << arg
                << " (use: deciduous | conifer | pine | --solid)\n";
        }
    }

    if (solidMode) {
        std::cout << "SOLID MODE enabled (light gray bark, no texture detail)\n";
    }


    fs::path root = FindProjectRoot();
    fs::path texRoot = root / "assets" / "textures";

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
        params.iterations = 13;

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

            // optional subtle spiral twist
            uv.x += uv.y * uBarkTwist;
            
            // macro noise from world pos
            float m  = noise3(vWorldPos * uMacroFreq);
            float m2 = noise3((vWorldPos + vec3(17.0, 5.0, 11.0)) * uMacroFreq);
            vec2 warp = vec2(m, m2) - 0.5;
            uv += warp * uUVWarp;

            vec3 albedo = texture(uAlbedoTex, uv).rgb * uBaseColor;
    
            // Tangent-space normal map
            vec3 nTS    = texture(uNormalTex, uv).xyz * 2.0 - 1.0;

            if (uFlipNormalY) nTS.y = -nTS.y;
            nTS.xy *= uNormalStrength;
            nTS = normalize(nTS);
    
            mat3 TBN = mat3(normalize(vT), normalize(vB), normalize(vN));
            vec3 N = normalize(TBN * nTS);
    
            vec3 L = normalize(uLightDir);
            float diff = max(dot(N, L), 0.0);
    
            vec3 V = normalize(uCamPos - vWorldPos);
            vec3 H = normalize(L + V);
    
            float rough = texture(uRoughTex,  uv).r;

            float macro = mix(1.0 - uMacroStrength, 1.0 + uMacroStrength, m);
            albedo *= macro;
            rough = clamp(rough + (m - 0.5) * 0.35 * uMacroStrength, 0.0, 1.0);

            float spec = pow(max(dot(N, H), 0.0), uSpecPower);
            spec *= uSpecStrength * (1.0 - rough);
    
            vec3 col = albedo * (uAmbient + diff) + vec3(spec);
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

    glm::vec3 camPos(0.0f, 10.0f, 45.0f);
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

        glUniform1f(uNormalStrLoc, 1.0f);
        glUniform1f(uSpecPowerLoc, 32.0f);
        glUniform1f(uSpecStrLoc, solidMode ? 0.15f : 0.35f);
        glUniform1i(uFlipNormalYLoc, 0); // if bumps look "inside out", change to 1

        //glUniform3f(uColorLoc, 0.55f, 0.27f, 0.07f);
        glUniform3f(uAmbientLoc, 0.75f, 0.75f, 0.75f);

        if (solidMode) glUniform3f(uAmbientLoc, 0.50f, 0.50f, 0.50f); // light gray
        else           glUniform3f(uAmbientLoc, 0.75f, 0.75f, 0.75f);

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

    glDeleteTextures(1, &texAlbedo);
    glDeleteTextures(1, &texNormal);
    glDeleteTextures(1, &texRough);

    glfwTerminate();
    return 0;
}
