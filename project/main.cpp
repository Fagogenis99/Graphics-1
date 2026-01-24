#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <iostream>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

// -------- camera state --------
static float gYaw   = 0.0f;  // rotation around Y axis
static float gPitch = 0.0f;  // rotation around X axis
static float gCamRadius = 4.0f;

// -------- pause / simulation time --------
static bool  gPaused  = false;
static float gSimTime = 0.0f;

// -------- scene parameters --------
static const int   kNumCubes = 6;

static const float kPlanetOrbitR = 1.2f;   // planet circle radius around origin
static const float kPlanetOrbitW = 0.6f;   // planet angular speed

static const float kCubeOrbitR   = 0.8f;   // cube orbit radius around planet
static const float kCubeOrbitW   = 1.4f;   // cube angular speed around planet

static void framebuffer_size_callback(GLFWwindow*, int w, int h) {
    glViewport(0, 0, w, h);
}

static void processInput(GLFWwindow* window) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

    // Camera rotation controls
    const float step = 0.02f;
    if (glfwGetKey(window, GLFW_KEY_LEFT)  == GLFW_PRESS) gYaw   -= step;
    if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) gYaw   += step;
    if (glfwGetKey(window, GLFW_KEY_UP)    == GLFW_PRESS) gPitch += step;
    if (glfwGetKey(window, GLFW_KEY_DOWN)  == GLFW_PRESS) gPitch -= step;

    // clamp pitch to avoid flipping
    if (gPitch >  1.4f) gPitch =  1.4f;
    if (gPitch < -1.4f) gPitch = -1.4f;

    // Toggle pause with P (edge-trigger: toggles once per press)
    static bool pWasDown = false;
    bool pDown = (glfwGetKey(window, GLFW_KEY_P) == GLFW_PRESS);
    if (pDown && !pWasDown) {
        gPaused = !gPaused;
        std::cout << (gPaused ? "PAUSED\n" : "RESUMED\n");
    }
    pWasDown = pDown;
}

static GLuint compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);

    int ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, 1024, nullptr, log);
        std::cerr << "Shader compile error:\n" << log << "\n";
    }
    return s;
}

static GLuint makeProgram(const char* vsSrc, const char* fsSrc) {
    GLuint vs = compileShader(GL_VERTEX_SHADER, vsSrc);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsSrc);

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);

    int ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(prog, 1024, nullptr, log);
        std::cerr << "Program link error:\n" << log << "\n";
    }

    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

static GLuint loadTexture2D(const char* path) {
    stbi_set_flip_vertically_on_load(true);

    int w, h, n;
    unsigned char* data = stbi_load(path, &w, &h, &n, 0);
    if (!data) {
        std::cerr << "Failed to load texture: " << path << "\n";
        return 0;
    }

    GLenum format = (n == 4) ? GL_RGBA : GL_RGB;

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glTexImage2D(GL_TEXTURE_2D, 0, format, w, h, 0, format, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);

    stbi_image_free(data);
    return tex;
}

int main() {
    if (!glfwInit()) {
        std::cerr << "GLFW init failed\n";
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(900, 700, "Project - Planet + 6 Cubes", nullptr, nullptr);
    if (!window) {
        std::cerr << "Window create failed\n";
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "GLAD init failed\n";
        glfwTerminate();
        return 1;
    }

    glEnable(GL_DEPTH_TEST);

    // Cube: position (x,y,z) + texcoord (u,v)
    float cubeVerts[] = {
        // back
        -0.5f,-0.5f,-0.5f,  0.0f,0.0f,
         0.5f,-0.5f,-0.5f,  1.0f,0.0f,
         0.5f, 0.5f,-0.5f,  1.0f,1.0f,
         0.5f, 0.5f,-0.5f,  1.0f,1.0f,
        -0.5f, 0.5f,-0.5f,  0.0f,1.0f,
        -0.5f,-0.5f,-0.5f,  0.0f,0.0f,

        // front
        -0.5f,-0.5f, 0.5f,  0.0f,0.0f,
         0.5f,-0.5f, 0.5f,  1.0f,0.0f,
         0.5f, 0.5f, 0.5f,  1.0f,1.0f,
         0.5f, 0.5f, 0.5f,  1.0f,1.0f,
        -0.5f, 0.5f, 0.5f,  0.0f,1.0f,
        -0.5f,-0.5f, 0.5f,  0.0f,0.0f,

        // left
        -0.5f, 0.5f, 0.5f,  1.0f,0.0f,
        -0.5f, 0.5f,-0.5f,  1.0f,1.0f,
        -0.5f,-0.5f,-0.5f,  0.0f,1.0f,
        -0.5f,-0.5f,-0.5f,  0.0f,1.0f,
        -0.5f,-0.5f, 0.5f,  0.0f,0.0f,
        -0.5f, 0.5f, 0.5f,  1.0f,0.0f,

        // right
         0.5f, 0.5f, 0.5f,  1.0f,0.0f,
         0.5f, 0.5f,-0.5f,  1.0f,1.0f,
         0.5f,-0.5f,-0.5f,  0.0f,1.0f,
         0.5f,-0.5f,-0.5f,  0.0f,1.0f,
         0.5f,-0.5f, 0.5f,  0.0f,0.0f,
         0.5f, 0.5f, 0.5f,  1.0f,0.0f,

        // bottom
        -0.5f,-0.5f,-0.5f,  0.0f,1.0f,
         0.5f,-0.5f,-0.5f,  1.0f,1.0f,
         0.5f,-0.5f, 0.5f,  1.0f,0.0f,
         0.5f,-0.5f, 0.5f,  1.0f,0.0f,
        -0.5f,-0.5f, 0.5f,  0.0f,0.0f,
        -0.5f,-0.5f,-0.5f,  0.0f,1.0f,

        // top
        -0.5f, 0.5f,-0.5f,  0.0f,1.0f,
         0.5f, 0.5f,-0.5f,  1.0f,1.0f,
         0.5f, 0.5f, 0.5f,  1.0f,0.0f,
         0.5f, 0.5f, 0.5f,  1.0f,0.0f,
        -0.5f, 0.5f, 0.5f,  0.0f,0.0f,
        -0.5f, 0.5f,-0.5f,  0.0f,1.0f
    };

    GLuint VAO, VBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVerts), cubeVerts, GL_STATIC_DRAW);

    // position
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // texcoord
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    const char* vsSrc = R"(
        #version 330 core
        layout (location = 0) in vec3 aPos;
        layout (location = 1) in vec2 aUV;

        out vec2 TexCoord;

        uniform mat4 model;
        uniform mat4 view;
        uniform mat4 projection;

        void main() {
            TexCoord = aUV;
            gl_Position = projection * view * model * vec4(aPos, 1.0);
        }
    )";

    const char* fsSrc = R"(
        #version 330 core
        in vec2 TexCoord;
        out vec4 FragColor;

        uniform sampler2D tex0;

        void main() {
            FragColor = texture(tex0, TexCoord);
        }
    )";

    GLuint prog = makeProgram(vsSrc, fsSrc);

    // change this path if needed
    GLuint tex = loadTexture2D("assets/textures/container.jpg");
    if (tex == 0) return 1;

    glUseProgram(prog);
    glUniform1i(glGetUniformLocation(prog, "tex0"), 0);

    while (!glfwWindowShouldClose(window)) {
        processInput(window);

        glClearColor(0.08f, 0.09f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // ---- time step (pause-able) ----
        static float lastRealTime = (float)glfwGetTime();
        float realTime = (float)glfwGetTime();
        float dt = realTime - lastRealTime;
        lastRealTime = realTime;

        if (!gPaused) {
            gSimTime += dt;
        }

        float t = gSimTime;

        // ---- camera view/projection ----
        glm::vec3 camPos;
        camPos.x = gCamRadius * cosf(gPitch) * sinf(gYaw);
        camPos.y = gCamRadius * sinf(gPitch);
        camPos.z = gCamRadius * cosf(gPitch) * cosf(gYaw);

        glm::mat4 view = glm::lookAt(camPos, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        float aspect = (h == 0) ? 1.0f : (float)w / (float)h;
        glm::mat4 projection = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);

        // ---- planet position (orbit around origin) ----
        glm::vec3 planetPos(
            kPlanetOrbitR * cosf(kPlanetOrbitW * t),
            0.0f,
            kPlanetOrbitR * sinf(kPlanetOrbitW * t)
        );

        glUseProgram(prog);
        glUniformMatrix4fv(glGetUniformLocation(prog, "view"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(prog, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex);

        glBindVertexArray(VAO);

        // ---- 6 orbiting cubes ----
        for (int i = 0; i < kNumCubes; ++i) {
            float base = (2.0f * 3.1415926f) * (float)i / (float)kNumCubes;
            float ang  = base + kCubeOrbitW * t;

            glm::vec3 cubePos = planetPos + glm::vec3(
                kCubeOrbitR * cosf(ang),
                0.2f * sinf(base * 3.0f),
                kCubeOrbitR * sinf(ang)
            );

            float selfW = 0.8f + 0.25f * i;

            glm::mat4 cubeModel(1.0f);
            cubeModel = glm::translate(cubeModel, cubePos);
            cubeModel = glm::rotate(cubeModel, selfW * t, glm::vec3(0.4f, 1.0f, 0.2f));
            cubeModel = glm::scale(cubeModel, glm::vec3(0.35f));

            glUniformMatrix4fv(glGetUniformLocation(prog, "model"), 1, GL_FALSE, glm::value_ptr(cubeModel));
            glDrawArrays(GL_TRIANGLES, 0, 36);
        }

        // ---- planet placeholder (big cube) ----
        glm::mat4 planetModel(1.0f);
        planetModel = glm::translate(planetModel, planetPos);
        planetModel = glm::scale(planetModel, glm::vec3(0.6f));

        glUniformMatrix4fv(glGetUniformLocation(prog, "model"), 1, GL_FALSE, glm::value_ptr(planetModel));
        glDrawArrays(GL_TRIANGLES, 0, 36);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glDeleteTextures(1, &tex);
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteProgram(prog);

    glfwTerminate();
    return 0;
}
