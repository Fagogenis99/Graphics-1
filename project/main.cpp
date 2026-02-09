#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm> // Απαραίτητο για το std::replace

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

// ---------------- Camera + Pause ----------------
static float gYaw = 0.0f;     
static float gPitch = 0.0f;   
static float gCamRadius = 8.0f;
static bool  gPaused = false;
static float gSimTime = 0.0f;

// ---------------- Scene Params ----------------
static const int   kNumCubes = 6;
static const float kPlanetOrbitR = 3.0f; 
static const float kPlanetOrbitW = 0.5f; 
static const float kCubeOrbitR = 2.0f;   
static const float kCubeOrbitW = 1.0f;   
static const float kCubeScale  = 0.35f;
static const float kPlanetScale = 0.2f;  

static void processInput(GLFWwindow* window, float dt) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

    const float camSpeed = 1.6f;
    const float step = camSpeed * dt;
    if (glfwGetKey(window, GLFW_KEY_LEFT)  == GLFW_PRESS) gYaw   -= step;
    if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) gYaw   += step;
    if (glfwGetKey(window, GLFW_KEY_UP)    == GLFW_PRESS) gPitch += step;
    if (glfwGetKey(window, GLFW_KEY_DOWN)  == GLFW_PRESS) gPitch -= step;

    if (gPitch >  1.4f) gPitch =  1.4f;
    if (gPitch < -1.4f) gPitch = -1.4f;

    static bool pWasDown = false;
    bool pDown = (glfwGetKey(window, GLFW_KEY_P) == GLFW_PRESS);
    if (pDown && !pWasDown) gPaused = !gPaused;
    pWasDown = pDown;
}

static GLuint compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    return s;
}

static GLuint makeProgram(const char* vsSrc, const char* fsSrc) {
    GLuint vs = compileShader(GL_VERTEX_SHADER, vsSrc);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsSrc);
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

static GLuint loadTexture2D(const char* path) {
    stbi_set_flip_vertically_on_load(true);
    int w, h, n;
    unsigned char* data = stbi_load(path, &w, &h, &n, 0);
    if (!data) return 0;
    GLenum format = (n == 4) ? GL_RGBA : GL_RGB;
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, format, w, h, 0, format, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    stbi_image_free(data);
    return tex;
}

struct MeshGL { GLuint VAO = 0; GLuint VBO = 0; GLsizei vertexCount = 0; };

static int fixIndex(int idx, int n) {
    if (idx > 0) return idx - 1;
    if (idx < 0) return n + idx;
    return -1;
}

static bool loadOBJ_to_interleaved(const std::string& path, std::vector<float>& out) {
    std::ifstream f(path);
    if (!f) return false;
    std::vector<glm::vec3> V;
    std::vector<glm::vec2> VT;
    std::vector<glm::vec3> VN;
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream iss(line);
        std::string tag; iss >> tag;
        if (tag == "v") { glm::vec3 p; iss >> p.x >> p.y >> p.z; V.push_back(p); }
        else if (tag == "vt") { glm::vec2 uv; iss >> uv.x >> uv.y; VT.push_back(uv); }
        else if (tag == "vn") { glm::vec3 n; iss >> n.x >> n.y >> n.z; VN.push_back(n); }
        else if (tag == "f") {
            std::vector<std::string> tokens; std::string t; while(iss >> t) tokens.push_back(t);
            for(size_t i=1; i+1 < tokens.size(); ++i) {
                int indices[3] = {0, (int)i, (int)i+1};
                for(int idx : indices) {
                    std::string segment = tokens[idx];
                    std::replace(segment.begin(), segment.end(), '/', ' ');
                    std::istringstream viss(segment);
                    int v_idx = -1, vt_idx = -1, vn_idx = -1;
                    viss >> v_idx >> vt_idx >> vn_idx;
                    
                    glm::vec3 p = V[fixIndex(v_idx, V.size())];
                    out.push_back(p.x); out.push_back(p.y); out.push_back(p.z);
                    
                    if (vn_idx != -1 && !VN.empty()) {
                        glm::vec3 n = VN[fixIndex(vn_idx, VN.size())];
                        out.push_back(n.x); out.push_back(n.y); out.push_back(n.z);
                    } else { out.push_back(0); out.push_back(1); out.push_back(0); }
                    
                    if (vt_idx != -1 && !VT.empty()) {
                        glm::vec2 uv = VT[fixIndex(vt_idx, VT.size())];
                        out.push_back(uv.x); out.push_back(uv.y);
                    } else { out.push_back(0); out.push_back(0); }
                }
            }
        }
    }
    return true;
}

static bool createMeshFromOBJ(const std::string& objPath, MeshGL& mesh) {
    std::vector<float> data;
    if (!loadOBJ_to_interleaved(objPath, data)) return false;
    mesh.vertexCount = (GLsizei)(data.size() / 8);
    glGenVertexArrays(1, &mesh.VAO); glGenBuffers(1, &mesh.VBO);
    glBindVertexArray(mesh.VAO);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.VBO);
    glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float), data.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);
    return true;
}

int main() {
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* window = glfwCreateWindow(1000, 800, "Graphics Assignment 2025", nullptr, nullptr);
    glfwMakeContextCurrent(window);
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
    glEnable(GL_DEPTH_TEST);

    const float cubeVerts[] = {
        -0.5f,-0.5f,-0.5f,  0,0,-1, 0,0,  0.5f,-0.5f,-0.5f,  0,0,-1, 1,0,  0.5f, 0.5f,-0.5f,  0,0,-1, 1,1,
         0.5f, 0.5f,-0.5f,  0,0,-1, 1,1, -0.5f, 0.5f,-0.5f,  0,0,-1, 0,1, -0.5f,-0.5f,-0.5f,  0,0,-1, 0,0,
        -0.5f,-0.5f, 0.5f,  0,0, 1, 0,0,  0.5f,-0.5f, 0.5f,  0,0, 1, 1,0,  0.5f, 0.5f, 0.5f,  0,0, 1, 1,1,
         0.5f, 0.5f, 0.5f,  0,0, 1, 1,1, -0.5f, 0.5f, 0.5f,  0,0, 1, 0,1, -0.5f,-0.5f, 0.5f,  0,0, 1, 0,0,
        -0.5f, 0.5f, 0.5f, -1,0,0, 1,0, -0.5f, 0.5f,-0.5f, -1,0,0, 1,1, -0.5f,-0.5f,-0.5f, -1,0,0, 0,1,
        -0.5f,-0.5f,-0.5f, -1,0,0, 0,1, -0.5f,-0.5f, 0.5f, -1,0,0, 0,0, -0.5f, 0.5f, 0.5f, -1,0,0, 1,0,
         0.5f, 0.5f, 0.5f,  1,0,0, 1,0,  0.5f, 0.5f,-0.5f,  1,0,0, 1,1,  0.5f,-0.5f,-0.5f,  1,0,0, 0,1,
         0.5f,-0.5f,-0.5f,  1,0,0, 0,1,  0.5f,-0.5f, 0.5f,  1,0,0, 0,0,  0.5f, 0.5f, 0.5f,  1,0,0, 1,0,
        -0.5f,-0.5f,-0.5f,  0,-1,0, 0,1,  0.5f,-0.5f,-0.5f,  0,-1,0, 1,1,  0.5f,-0.5f, 0.5f,  0,-1,0, 1,0,
         0.5f,-0.5f, 0.5f,  0,-1,0, 1,0, -0.5f,-0.5f, 0.5f,  0,-1,0, 0,0, -0.5f,-0.5f,-0.5f,  0,-1,0, 0,1,
        -0.5f, 0.5f,-0.5f,  0, 1,0, 0,1,  0.5f, 0.5f,-0.5f,  0, 1,0, 1,1,  0.5f, 0.5f, 0.5f,  0, 1,0, 1,0,
         0.5f, 0.5f, 0.5f,  0, 1,0, 1,0, -0.5f, 0.5f, 0.5f,  0, 1,0, 0,0, -0.5f, 0.5f,-0.5f,  0, 1,0, 0,1
    };

    GLuint cubeVAO, cubeVBO;
    glGenVertexArrays(1, &cubeVAO); glGenBuffers(1, &cubeVBO);
    glBindVertexArray(cubeVAO);
    glBindBuffer(GL_ARRAY_BUFFER, cubeVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVerts), cubeVerts, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    MeshGL planetMesh;
    createMeshFromOBJ("assets/objects/planet.obj", planetMesh);
    GLuint cubeTex = loadTexture2D("assets/textures/container.jpg");

    const char* cubeVS = R"(#version 330 core
        layout (location = 0) in vec3 aPos; layout (location = 1) in vec3 aNormal; layout (location = 2) in vec2 aUV;
        out vec3 FragPos; out vec3 Normal; out vec2 TexCoord;
        uniform mat4 model, view, projection;
        void main() {
            FragPos = vec3(model * vec4(aPos, 1.0));
            Normal = mat3(transpose(inverse(model))) * aNormal;
            TexCoord = aUV;
            gl_Position = projection * view * vec4(FragPos, 1.0);
        })";

    const char* cubeFS = R"(#version 330 core
        out vec4 FragColor; in vec3 FragPos; in vec3 Normal; in vec2 TexCoord;
        uniform sampler2D tex0; uniform vec3 lightPos;
        void main() {
            vec3 albedo = texture(tex0, TexCoord).rgb;
            vec3 norm = normalize(Normal);
            vec3 lightDir = normalize(lightPos - FragPos);
            float diff = max(dot(norm, lightDir), 0.0);
            FragColor = vec4((0.2 + diff) * albedo, 1.0);
        })";

    const char* planetVS = R"(#version 330 core
        layout (location = 0) in vec3 aPos;
        uniform mat4 model, view, projection;
        void main() { gl_Position = projection * view * model * vec4(aPos, 1.0); })";

    const char* planetFS = R"(#version 330 core
        out vec4 FragColor;
        void main() { FragColor = vec4(1.0, 0.9, 0.5, 1.0); })";

    GLuint cubeProg = makeProgram(cubeVS, cubeFS);
    GLuint planetProg = makeProgram(planetVS, planetFS);

    while (!glfwWindowShouldClose(window)) {
        static float lastTime = 0.0f;
        float currTime = (float)glfwGetTime();
        float dt = currTime - lastTime; lastTime = currTime;
        processInput(window, dt);
        if (!gPaused) gSimTime += dt;

        glClearColor(0.05f, 0.05f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glm::vec3 planetPos(cos(gSimTime * kPlanetOrbitW) * kPlanetOrbitR, 0, sin(gSimTime * kPlanetOrbitW) * kPlanetOrbitR);
        glm::vec3 camPos(gCamRadius * cos(gPitch) * sin(gYaw), gCamRadius * sin(gPitch), gCamRadius * cos(gPitch) * cos(gYaw));
        glm::mat4 view = glm::lookAt(camPos, glm::vec3(0,0,0), glm::vec3(0,1,0));
        glm::mat4 proj = glm::perspective(glm::radians(45.0f), 1000.0f/800.0f, 0.1f, 100.0f);

        glUseProgram(planetProg);
        glm::mat4 model = glm::translate(glm::mat4(1.0f), planetPos);
        model = glm::scale(model, glm::vec3(kPlanetScale));
        glUniformMatrix4fv(glGetUniformLocation(planetProg, "model"), 1, GL_FALSE, glm::value_ptr(model));
        glUniformMatrix4fv(glGetUniformLocation(planetProg, "view"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(planetProg, "projection"), 1, GL_FALSE, glm::value_ptr(proj));
        glBindVertexArray(planetMesh.VAO); glDrawArrays(GL_TRIANGLES, 0, planetMesh.vertexCount);

        glUseProgram(cubeProg);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, cubeTex);
        glUniform3fv(glGetUniformLocation(cubeProg, "lightPos"), 1, glm::value_ptr(planetPos));
        glUniformMatrix4fv(glGetUniformLocation(cubeProg, "view"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(cubeProg, "projection"), 1, GL_FALSE, glm::value_ptr(proj));

        glBindVertexArray(cubeVAO);
        for(int i=0; i<kNumCubes; ++i) {
            float off = (2.0f * 3.14159f * i) / kNumCubes;
            glm::vec3 cPos = planetPos + glm::vec3(cos(gSimTime * kCubeOrbitW + off) * kCubeOrbitR, sin(off)*0.5f, sin(gSimTime * kCubeOrbitW + off) * kCubeOrbitR);
            model = glm::translate(glm::mat4(1.0f), cPos);
            model = glm::rotate(model, gSimTime * (1.0f + i * 0.5f), glm::vec3(0.5, 1, 0));
            model = glm::scale(model, glm::vec3(kCubeScale));
            glUniformMatrix4fv(glGetUniformLocation(cubeProg, "model"), 1, GL_FALSE, glm::value_ptr(model));
            glDrawArrays(GL_TRIANGLES, 0, 36);
        }
        glfwSwapBuffers(window); glfwPollEvents();
    }
    glfwTerminate(); return 0;
}