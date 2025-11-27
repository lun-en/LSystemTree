#include <algorithm>
#include <iostream>
#include <memory>
#include <vector>
#include <string>

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

 void loadModels() {
  ctx.models.clear();
   // Use the cube model as our branch primitive 
   Model* treeBranchModel = Model::fromObjectFile("../assets/models/cube/cube.obj");
   // Optional: set default model matrix to identity (should already be)
   // treeBranchModel->modelMatrix = glm::mat4(1.0f);
  
   // 2. Load a texture (for now, reuse the dice texture)
   GLuint barkTexture = createTexture("../assets/models/cube/dice.jpg");
   treeBranchModel->textures.push_back(barkTexture);

   // 3. Store model in context
   ctx.models.push_back(treeBranchModel);

   // debug for texture model
   std::cout << "Models loaded: " << ctx.models.size() << std::endl;
   if (!ctx.models.empty()) {
     std::cout << "Textures in model 0: " << ctx.models[0]->textures.size() << std::endl;
   }
 }

 void buildLSystemTreeObjects(int branchModelIndex) {
   //1. Generate the L-system string
   LSystem lsys;
   lsys.setAxiom("A");
   lsys.addRule('A', "F[+A][-A]L"); 

   int iterations = 3;  // 3-5 is okk
   std::string sentence = lsys.generate(iterations);
   std::cout << "L-system sentence length: " << sentence.size() << std::endl;

   // 2. Turtle parameters
   float branchLength = 1.0f;  // length of one segment
   float branchRadius = 0.1f;  // thickness
   float angleDeg = 25.0f;     // rotation angle for + and -

   // Start at origin, move base down so tree is centered in view
   glm::mat4 current = glm::mat4(1.0f);
   current = glm::translate(current, glm::vec3(0.0f, -3.0f, 0.0f));

   std::vector<glm::mat4> stack;

   //3. Parse the L-system string
   for (char c : sentence) {
     switch (c) {
       // Treat A as F
       case 'F':
       case 'A': {
         // cube is roughly [-0.5,0.5] in all axes, so:
         // 1) scale it to (radius x length x radius),
         // 2) translate it up by half its length so its base sits at the turtle origin
         glm::mat4 model = current;
         model = model * glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.5f * branchLength, 0.0f));
         model = model * glm::scale(glm::mat4(1.0f), glm::vec3(branchRadius, branchLength, branchRadius));

         // Object instance for this segment
         Object* obj = new Object(branchModelIndex, model);
         obj->material = mTreeMaterial;  //brown material
         obj->textureIndex = 0;          // use the first texture in the cube model (dice.jpg for now)
         ctx.objects.push_back(obj);

         // Move turtle up to the tip of the segment
         current = current * glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, branchLength, 0.0f));
         break;
       }

       case '+':
         // Rotate around Z axis by +angle
         current = current * glm::rotate(glm::mat4(1.0f), glm::radians(angleDeg), glm::vec3(0.0f, 0.0f, 1.0f));
         break;

       case '-':
         // Rotate around Z axis by -angle
         current = current * glm::rotate(glm::mat4(1.0f), glm::radians(-angleDeg), glm::vec3(0.0f, 0.0f, 1.0f));
         break;

       case '[':
         // Save current transform with push
         stack.push_back(current);
         break;

       case ']':
         // Restore last transform with pop
         if (!stack.empty()) {
           current = stack.back();
           stack.pop_back();
         }
         break;
       case 'L': {
         // Small, flat “leaf” using the same cube model, just scaled down
         float leafSize = 0.3f;

         glm::mat4 model = current;
         // Put the leaf just above the branch origin
         model = model * glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.5f * leafSize, 0.0f));
         model = model * glm::scale(glm::mat4(1.0f), glm::vec3(leafSize, leafSize, leafSize));

         Object* leaf = new Object(branchModelIndex, model);
         leaf->material = mLeafMaterial;
         leaf->textureIndex = 0;  // same texture for now (or you can later use a leaf texture)
         ctx.objects.push_back(leaf);
         break;
       }


       default:
         // Ignore any other
         break;
     }
   }
 }



 void setupObjects() {
  
    ctx.objects.clear();
   //the branch primitive   index 0
   int branchModelIndex = 0;
   // Build tree objects from the string
   buildLSystemTreeObjects(branchModelIndex);

   // Later, you can add ground plane or other objects here as well.
   std::cout << "Objects created: " << ctx.objects.size() << std::endl;

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