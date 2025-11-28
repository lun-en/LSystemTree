#include <algorithm>
#include <iostream>
#include <memory>
#include <vector>
#include <string>
#include<cmath>

#include "stb_image.h"
#include <GLFW/glfw3.h>
#define GLAD_GL_IMPLEMENTATION
#include <glad/gl.h>
#undef GLAD_GL_IMPLEMENTATION
#include <glm/glm.hpp>

#include <glm/ext/matrix_transform.hpp>

#include "camera.h"
#include "context.h"
#include "gl_helper.h"
#include "model.h"
#include "opengl_context.h"
#include "program.h"
#include "utils.h"

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_glfw.h"

#include "LSystem.h"

void initOpenGL();
void resizeCallback(GLFWwindow* window, int width, int height);
void keyCallback(GLFWwindow* window, int key, int, int action, int);

Context ctx;

Material mTreeMaterial;
Material mLeafMaterial;


void loadMaterial() {
  
    // A basic brown/wood material for the tree
  mTreeMaterial.ambient = glm::vec3(0.2f, 0.2f, 0.2f);
  mTreeMaterial.diffuse = glm::vec3(0.6f, 0.4f, 0.2f);  // Brown
  mTreeMaterial.specular = glm::vec3(0.1f, 0.1f, 0.1f);
  mTreeMaterial.shininess = 10.0f;

  // Leaf material (green)
  mLeafMaterial.ambient = glm::vec3(0.0f, 0.2f, 0.0f);
  mLeafMaterial.diffuse = glm::vec3(0.1f, 0.6f, 0.1f);
  mLeafMaterial.specular = glm::vec3(0.1f);
  mLeafMaterial.shininess = 4.0f;

}

void loadPrograms() {
  // Only load the LightProgram for now
  ctx.programs.push_back(new LightProgram(&ctx));

  for (auto iter = ctx.programs.begin(); iter != ctx.programs.end(); iter++) {
    if (!(*iter)->load()) {
      std::cout << "Load program fail, force terminate" << std::endl;
      exit(1);
    }
  }
  glUseProgram(0);
}

void appendFrustumSegment(Model* model, float length, float radiusBottom, float radiusTop, const glm::mat4& transform, int radialSegments) {
  // build independent triangles (no shared vertices)
  // For each side, we create 2 triangles = 6 vertices.
  const float TWO_PI = 6.28318530718f;

  for (int i = 0; i < radialSegments; ++i) {
    float t0 = static_cast<float>(i) / radialSegments;
    float t1 = static_cast<float>(i + 1) / radialSegments;

    float angle0 = t0 * TWO_PI;
    float angle1 = t1 * TWO_PI;

    // Positions in LOCAL space of the frustum
    glm::vec3 p0b(radiusBottom * std::cos(angle0), 0.0f, radiusBottom * std::sin(angle0));  // bottom ring, segment i
    glm::vec3 p1b(radiusBottom * std::cos(angle1), 0.0f, radiusBottom * std::sin(angle1));  // bottom ring, segment i+1

    glm::vec3 p0t(radiusTop * std::cos(angle0), length, radiusTop * std::sin(angle0));  // top ring
    glm::vec3 p1t(radiusTop * std::cos(angle1), length, radiusTop * std::sin(angle1));

    // Approximate normal direction using the mid-angle (good enough)
    float angleMid = 0.5f * (angle0 + angle1);
    glm::vec3 localNormal(std::cos(angleMid), 0.0f, std::sin(angleMid));

    // Transform positions to WORLD space
    glm::vec4 tp0b = transform * glm::vec4(p0b, 1.0f);
    glm::vec4 tp1b = transform * glm::vec4(p1b, 1.0f);
    glm::vec4 tp0t = transform * glm::vec4(p0t, 1.0f);
    glm::vec4 tp1t = transform * glm::vec4(p1t, 1.0f);

    // Transform normal: use upper-left 3x3 of transform
    glm::mat3 normalMatrix = glm::mat3(transform);
    glm::vec3 worldNormal = glm::normalize(normalMatrix * localNormal);

    // Simple UVs along circumference (t) and height (v = 0..1)
    float u0 = t0;
    float u1 = t1;
    float v0 = 0.0f;
    float v1 = 1.0f;

    auto pushVertex = [&](const glm::vec4& pos, float u, float v) {
      model->positions.push_back(pos.x);
      model->positions.push_back(pos.y);
      model->positions.push_back(pos.z);

      model->normals.push_back(worldNormal.x);
      model->normals.push_back(worldNormal.y);
      model->normals.push_back(worldNormal.z);

      model->texcoords.push_back(u);
      model->texcoords.push_back(v);

      model->numVertex++;
    };

    // Triangle 1: p0b, p0t, p1t
    pushVertex(tp0b, u0, v0);
    pushVertex(tp0t, u0, v1);
    pushVertex(tp1t, u1, v1);

    // Triangle 2: p0b, p1t, p1b
    pushVertex(tp0b, u0, v0);
    pushVertex(tp1t, u1, v1);
    pushVertex(tp1b, u1, v0);
  }
}


Model* buildTreeModelFromLSystem();
// helper to append one frustum segment into a Model (drawBranchSegment)
void appendFrustumSegment(Model* model, float length, float radiusBottom, float radiusTop, const glm::mat4& transform,
                          int radialSegments);

struct TurtleState {
  glm::mat4 transform;
  float radius;
  float length;
  int depth;
};

Model* buildTreeModelFromLSystem() {
  Model* model = new Model();
  model->positions.clear();
  model->normals.clear();
  model->texcoords.clear();
  model->textures.clear();
  model->numVertex = 0;
 

  // ----- 1. Generate L-system string -----
  LSystem lsys;
  lsys.setAxiom("A");
  lsys.addRule('A', "F[+A][-A]");  // structure
  //int iterations = 5;              // tweak later
  int iterations = 4;

  std::string sentence = lsys.generate(iterations);
  std::cout << "L-system sentence length: " << sentence.size() << std::endl;

  // ----- 2. Turtle parameters -----
  float baseRadius = 0.5f;
  float baseLength = 1.5f;
  float radiusDecayF = 0.85f;
  float lengthDecayF = 0.95f;
  float branchRadiusDecay = 0.7f;
  float branchAngleDeg = 25.0f;
  int radialSegments = 12;  // how many sides on cylinder

  // Initial turtle state
  TurtleState current;
  current.transform = glm::mat4(1.0f);
  current.transform = glm::translate(current.transform, glm::vec3(0.0f, -3.0f, 0.0f));  // move base down
  current.radius = baseRadius;
  current.length = baseLength;
  current.depth = 0;

  std::vector<TurtleState> stack;

  // ----- 3. Parse sentence & build geometry -----
  for (char c : sentence) {
    switch (c) {
      case 'F':
      case 'A': {
        float rBottom = current.radius;
        float rTop = current.radius * radiusDecayF;
        float len = current.length;

        if (rBottom > 0.01f && len > 0.05f) {
          appendFrustumSegment(model, len, rBottom, rTop, current.transform, radialSegments);
        }

        // Move turtle to the tip in its local +Y direction
        current.transform = current.transform * glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, len, 0.0f));

        // Update radius & length for next segment
        current.radius = rTop;
        current.length = current.length * lengthDecayF;
        current.depth += 1;
        break;
      }

      case '+':
        current.transform =
            current.transform * glm::rotate(glm::mat4(1.0f), glm::radians(branchAngleDeg), glm::vec3(0.0f, 0.0f, 1.0f));
        break;

      case '-':
        current.transform = current.transform *
                            glm::rotate(glm::mat4(1.0f), glm::radians(-branchAngleDeg), glm::vec3(0.0f, 0.0f, 1.0f));
        break;

      case '[': {
        // Save current state
        stack.push_back(current);
        // Shrink radius/length for new branch
        current.radius *= branchRadiusDecay;
        current.length *= lengthDecayF;
        break;
      }

      case ']':
        if (!stack.empty()) {
          current = stack.back();
          stack.pop_back();
        }
        break;

      default:
        break;
    }
  }

  // ----- 4. Load a bark texture for this model -----
  GLuint barkTexture = createTexture("../assets/models/cube/dice.jpg");  // reuse for now
  model->textures.push_back(barkTexture);

  return model;
}



 void loadModels() {
  ctx.models.clear();
   Model* treeModel = buildTreeModelFromLSystem();
   ctx.models.push_back(treeModel);

   //debugging
   std::cout << "Models loaded: " << ctx.models.size() << "\n";
   if (!ctx.models.empty()) {
     std::cout << "Tree model vertices: " << ctx.models[0]->numVertex << "\n";
     std::cout << "Tree textures: " << ctx.models[0]->textures.size() << "\n";
   }
 }

 

 void setupObjects() {
    ctx.objects.clear();
    
    // Single object that uses the tree model at index 0
    glm::mat4 identity(1.0f);
    Object* treeObject = new Object(0, identity);
    treeObject->material = mTreeMaterial;  //brown tree material
    treeObject->textureIndex = 0;          // use first texture

    ctx.objects.push_back(treeObject);

    //debug 
    std::cout << "Objects: " << ctx.objects.size() << "\n";

 }



int main() {
  initOpenGL();
  GLFWwindow* window = OpenGLContext::getWindow();
  // Update Title
  glfwSetWindowTitle(window, "Final Project: L-Systems");

  // Quick test of the L-system 
  {
    LSystem sys;
    sys.setAxiom("A");
    // Simple example: A → F[+A][-A]
    sys.addRule('A', "F[+A][-A]");

    std::string result = sys.generate(3);
    std::cout << "L-system result after 3 iterations: " << result << std::endl;
  }

  // Init Camera helper
  Camera camera(glm::vec3(0, 5, 15));
  camera.initialize(OpenGLContext::getAspectRatio());
  // Store camera as glfw global variable for callbacks use
  glfwSetWindowUserPointer(window, &camera);
  ctx.camera = &camera;
  ctx.window = window;

  // Initialization
  loadMaterial();
  loadModels();
  loadPrograms();
  setupObjects();

  //ImGui setup
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  (void)io;
  ImGui::StyleColorsDark();
  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 330 core");
 
  // Main rendering loop
  while (!glfwWindowShouldClose(window)) {
    // Polling events.
    glfwPollEvents();
    // Update camera position and view
    camera.move(window);

    // GL_XXX_BIT can simply "OR" together to use.
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);//
    /// TO DO Enable DepthTest
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glClearDepth(1.0f);

    //render objects
    for (size_t i = 0; i < ctx.programs.size(); i++) {
      ctx.programs[i]->doMainLoop();
    }

    //render UI
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    // control panel
    {
      ImGui::Begin("L-Sytems Control");
      ImGui::Text("Tree Generation will go here.");
      ImGui::End();
    }
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

#ifdef __APPLE__
    // Some platform need explicit glFlush
    glFlush();
#endif
    glfwSwapBuffers(window);
  }
  // Cleanup ImGui
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  return 0;
}

void keyCallback(GLFWwindow* window, int key, int, int action, int) {
  // Press ESC to close the window.
  if (key == GLFW_KEY_ESCAPE) {
    glfwSetWindowShouldClose(window, GLFW_TRUE);
    return;
  }
  if (action == GLFW_PRESS) {
    switch (key) {
      case GLFW_KEY_F1: {
        // Toggle cursor
        Camera* cam = static_cast<Camera*>(glfwGetWindowUserPointer(window));
        if (glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED) {
          glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
          int ww, hh;
          glfwGetWindowSize(window, &ww, &hh);
          glfwSetCursorPos(window, static_cast<double>(ww) / 2.0, static_cast<double>(hh) / 2.0);
          if (cam) cam->setLastMousePos(window);
        } else {
          glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
          if (cam) cam->setLastMousePos(window);
        }
        break;
      }
      default:
        break;
    }
  }
}

void resizeCallback(GLFWwindow* window, int width, int height) {
  OpenGLContext::framebufferResizeCallback(window, width, height);
  auto ptr = static_cast<Camera*>(glfwGetWindowUserPointer(window));
  if (ptr) {
    ptr->updateProjectionMatrix(OpenGLContext::getAspectRatio());
  }
}

void initOpenGL() {
  // Initialize OpenGL context
#ifdef __APPLE__
  // MacOS need explicit request legacy support
  OpenGLContext::createContext(21, GLFW_OPENGL_ANY_PROFILE);
#else
  OpenGLContext::createContext(21, GLFW_OPENGL_ANY_PROFILE);
//  OpenGLContext::createContext(43, GLFW_OPENGL_COMPAT_PROFILE);
#endif
  GLFWwindow* window = OpenGLContext::getWindow();
  glfwSetKeyCallback(window, keyCallback);
  glfwSetFramebufferSizeCallback(window, resizeCallback);
  glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
#ifndef NDEBUG
  OpenGLContext::printSystemInfo();
  // This is useful if you want to debug your OpenGL API calls.
  OpenGLContext::enableDebugCallback();
#endif
}