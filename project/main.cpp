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

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

// ---------------- Camera + Pause ----------------
static float gYaw = 0.0f;     // around Y
static float gPitch = 0.0f;   // around X
static float gCamRadius = 5.0f;

static bool  gPaused = false;
static float gSimTime = 0.0f;

// ---------------- Scene Params ----------------
static const int   kNumCubes = 6;
static const float kCubeOrbitR = 2.6f;   // orbit radius around planet
static const float kCubeOrbitW = 1.0f;   // angular speed (around planet)
static const float kCubeScale  = 0.35f;

static const float kPlanetScale = 0.4f;  // scale of planet.obj
static const float kPlanetSpinW = 0.4f;  // self-rotation speed of planet (optional)

// planet is center (matches “cubes around the sphere”)
static const glm::vec3 kPlanetPos(0.0f, 0.0f, 0.0f);

// --------------- GLFW callbacks ---------------
static void framebuffer_size_callback(GLFWwindow*, int w, int h) {
    glViewport(0, 0, w, h);
}

static void processInput(GLFWwindow* window, float dt) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

    const float camSpeed = 1.6f;      // radians per second (tweak)
    const float step = camSpeed * dt; // frame-rate independent
    if (glfwGetKey(window, GLFW_KEY_LEFT)  == GLFW_PRESS) gYaw   -= step;
    if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) gYaw   += step;
    if (glfwGetKey(window, GLFW_KEY_UP)    == GLFW_PRESS) gPitch += step;
    if (glfwGetKey(window, GLFW_KEY_DOWN)  == GLFW_PRESS) gPitch -= step;

    if (gPitch >  1.4f) gPitch =  1.4f;
    if (gPitch < -1.4f) gPitch = -1.4f;

    // Toggle pause with P (edge-trigger)
    static bool pWasDown = false;
    bool pDown = (glfwGetKey(window, GLFW_KEY_P) == GLFW_PRESS);
    if (pDown && !pWasDown) {
        gPaused = !gPaused;
        std::cout << (gPaused ? "PAUSED\n" : "RESUMED\n");
    }
    pWasDown = pDown;
}

// ---------------- Shader helpers ----------------
static GLuint compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);

    int ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(s, 2048, nullptr, log);
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
        char log[2048];
        glGetProgramInfoLog(prog, 2048, nullptr, log);
        std::cerr << "Program link error:\n" << log << "\n";
    }

    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

// ---------------- Texture loader ----------------
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

// ---------------- Minimal OBJ loader ----------------
// Supports: v, vt, vn, f (triangles or polygons -> triangulated fan)
// Produces interleaved: pos(3), normal(3), uv(2) per vertex
struct MeshGL {
    GLuint VAO = 0;
    GLuint VBO = 0;
    GLsizei vertexCount = 0; // number of vertices (not triangles)
};

static int fixIndex(int idx, int n) {
    // OBJ: 1-based, negative allowed
    if (idx > 0) return idx - 1;
    if (idx < 0) return n + idx;
    return -1;
}

static bool loadOBJ_to_interleaved(const std::string& path, std::vector<float>& out) {
    std::ifstream f(path);
    if (!f) {
        std::cerr << "Cannot open OBJ: " << path << "\n";
        return false;
    }

    std::vector<glm::vec3> V;
    std::vector<glm::vec2> VT;
    std::vector<glm::vec3> VN;

    struct Idx { int v=-1, vt=-1, vn=-1; };

    auto parseVertexRef = [&](const std::string& token) -> Idx {
        // formats: v, v/vt, v//vn, v/vt/vn
        Idx r;
        std::string a, b, c;
        size_t p1 = token.find('/');
        if (p1 == std::string::npos) {
            a = token;
        } else {
            a = token.substr(0, p1);
            size_t p2 = token.find('/', p1 + 1);
            if (p2 == std::string::npos) {
                b = token.substr(p1 + 1);
            } else {
                b = token.substr(p1 + 1, p2 - (p1 + 1));
                c = token.substr(p2 + 1);
            }
        }

        if (!a.empty()) r.v  = std::stoi(a);
        if (!b.empty()) r.vt = std::stoi(b);
        if (!c.empty()) r.vn = std::stoi(c);

        r.v  = fixIndex(r.v,  (int)V.size());
        r.vt = (b.empty() ? -1 : fixIndex(r.vt, (int)VT.size()));
        r.vn = (c.empty() ? -1 : fixIndex(r.vn, (int)VN.size()));
        return r;
    };

    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;

        std::istringstream iss(line);
        std::string tag;
        iss >> tag;

        if (tag == "v") {
            glm::vec3 p;
            iss >> p.x >> p.y >> p.z;
            V.push_back(p);
        } else if (tag == "vt") {
            glm::vec2 uv;
            iss >> uv.x >> uv.y;
            VT.push_back(uv);
        } else if (tag == "vn") {
            glm::vec3 n;
            iss >> n.x >> n.y >> n.z;
            VN.push_back(glm::normalize(n));
        } else if (tag == "f") {
            std::vector<Idx> face;
            std::string tok;
            while (iss >> tok) face.push_back(parseVertexRef(tok));
            if (face.size() < 3) continue;

            // triangulate fan: (0, i, i+1)
            for (size_t i = 1; i + 1 < face.size(); ++i) {
                Idx a = face[0];
                Idx b = face[i];
                Idx c = face[i + 1];

                glm::vec3 pa = V[a.v], pb = V[b.v], pc = V[c.v];

                // if no normals provided, compute per-triangle normal
                glm::vec3 na(0), nb(0), nc(0);
                bool hasNormals = (a.vn >= 0 && b.vn >= 0 && c.vn >= 0) && !VN.empty();
                if (hasNormals) {
                    na = VN[a.vn]; nb = VN[b.vn]; nc = VN[c.vn];
                } else {
                    glm::vec3 n = glm::normalize(glm::cross(pb - pa, pc - pa));
                    na = nb = nc = n;
                }

                glm::vec2 uva(0), uvb(0), uvc(0);
                bool hasUV = (a.vt >= 0 && b.vt >= 0 && c.vt >= 0) && !VT.empty();
                if (hasUV) {
                    uva = VT[a.vt]; uvb = VT[b.vt]; uvc = VT[c.vt];
                }

                auto pushVert = [&](const glm::vec3& p, const glm::vec3& n, const glm::vec2& uv) {
                    out.push_back(p.x);  out.push_back(p.y);  out.push_back(p.z);
                    out.push_back(n.x);  out.push_back(n.y);  out.push_back(n.z);
                    out.push_back(uv.x); out.push_back(uv.y);
                };

                pushVert(pa, na, uva);
                pushVert(pb, nb, uvb);
                pushVert(pc, nc, uvc);
            }
        }
    }

    if (out.empty()) {
        std::cerr << "OBJ produced no vertices: " << path << "\n";
        return false;
    }
    return true;
}

static bool createMeshFromOBJ(const std::string& objPath, MeshGL& mesh) {
    std::vector<float> data;
    if (!loadOBJ_to_interleaved(objPath, data)) return false;

    mesh.vertexCount = (GLsizei)(data.size() / 8); // 8 floats per vertex

    glGenVertexArrays(1, &mesh.VAO);
    glGenBuffers(1, &mesh.VBO);

    glBindVertexArray(mesh.VAO);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.VBO);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(data.size() * sizeof(float)), data.data(), GL_STATIC_DRAW);

    // layout: pos(3), normal(3), uv(2)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);
    return true;
}

// ---------------- Main ----------------
int main() {
    if (!glfwInit()) {
        std::cerr << "GLFW init failed\n";
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(1000, 800, "Project - Planet OBJ + 6 Textured Cubes", nullptr, nullptr);
    if (!window) {
        std::cerr << "Window create failed\n";
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // vsync ON

    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "GLAD init failed\n";
        glfwTerminate();
        return 1;
    }

    glEnable(GL_DEPTH_TEST);

    // ---------------- Cube vertex data (hardcoded) ----------------
    // Interleaved: pos(3), normal(3), uv(2)
    const float cubeVerts[] = {
        // back face (0,0,-1)
        -0.5f,-0.5f,-0.5f,  0,0,-1,  0,0,
         0.5f,-0.5f,-0.5f,  0,0,-1,  1,0,
         0.5f, 0.5f,-0.5f,  0,0,-1,  1,1,
         0.5f, 0.5f,-0.5f,  0,0,-1,  1,1,
        -0.5f, 0.5f,-0.5f,  0,0,-1,  0,1,
        -0.5f,-0.5f,-0.5f,  0,0,-1,  0,0,

        // front face (0,0,1)
        -0.5f,-0.5f, 0.5f,  0,0, 1,  0,0,
         0.5f,-0.5f, 0.5f,  0,0, 1,  1,0,
         0.5f, 0.5f, 0.5f,  0,0, 1,  1,1,
         0.5f, 0.5f, 0.5f,  0,0, 1,  1,1,
        -0.5f, 0.5f, 0.5f,  0,0, 1,  0,1,
        -0.5f,-0.5f, 0.5f,  0,0, 1,  0,0,

        // left face (-1,0,0)
        -0.5f, 0.5f, 0.5f, -1,0,0,  1,0,
        -0.5f, 0.5f,-0.5f, -1,0,0,  1,1,
        -0.5f,-0.5f,-0.5f, -1,0,0,  0,1,
        -0.5f,-0.5f,-0.5f, -1,0,0,  0,1,
        -0.5f,-0.5f, 0.5f, -1,0,0,  0,0,
        -0.5f, 0.5f, 0.5f, -1,0,0,  1,0,

        // right face (1,0,0)
         0.5f, 0.5f, 0.5f,  1,0,0,  1,0,
         0.5f, 0.5f,-0.5f,  1,0,0,  1,1,
         0.5f,-0.5f,-0.5f,  1,0,0,  0,1,
         0.5f,-0.5f,-0.5f,  1,0,0,  0,1,
         0.5f,-0.5f, 0.5f,  1,0,0,  0,0,
         0.5f, 0.5f, 0.5f,  1,0,0,  1,0,

        // bottom face (0,-1,0)
        -0.5f,-0.5f,-0.5f,  0,-1,0,  0,1,
         0.5f,-0.5f,-0.5f,  0,-1,0,  1,1,
         0.5f,-0.5f, 0.5f,  0,-1,0,  1,0,
         0.5f,-0.5f, 0.5f,  0,-1,0,  1,0,
        -0.5f,-0.5f, 0.5f,  0,-1,0,  0,0,
        -0.5f,-0.5f,-0.5f,  0,-1,0,  0,1,

        // top face (0,1,0)
        -0.5f, 0.5f,-0.5f,  0, 1,0,  0,1,
         0.5f, 0.5f,-0.5f,  0, 1,0,  1,1,
         0.5f, 0.5f, 0.5f,  0, 1,0,  1,0,
         0.5f, 0.5f, 0.5f,  0, 1,0,  1,0,
        -0.5f, 0.5f, 0.5f,  0, 1,0,  0,0,
        -0.5f, 0.5f,-0.5f,  0, 1,0,  0,1
    };

    GLuint cubeVAO, cubeVBO;
    glGenVertexArrays(1, &cubeVAO);
    glGenBuffers(1, &cubeVBO);

    glBindVertexArray(cubeVAO);
    glBindBuffer(GL_ARRAY_BUFFER, cubeVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVerts), cubeVerts, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);

    // ---------------- Load planet OBJ ----------------
    MeshGL planetMesh;
    if (!createMeshFromOBJ("assets/objects/planet.obj", planetMesh)) {
        std::cerr << "Planet OBJ load failed. Check path: assets/objects/planet.obj\n";
        return 1;
    }

    // ---------------- Shaders ----------------
    // CUBES: textured + Phong lighting (planet acts as point light)
    const char* cubeVS = R"(
        #version 330 core
        layout (location = 0) in vec3 aPos;
        layout (location = 1) in vec3 aNormal;
        layout (location = 2) in vec2 aUV;

        out vec3 FragPos;
        out vec3 Normal;
        out vec2 TexCoord;

        uniform mat4 model;
        uniform mat4 view;
        uniform mat4 projection;

        void main() {
            vec4 worldPos = model * vec4(aPos, 1.0);
            FragPos = worldPos.xyz;
            Normal = mat3(transpose(inverse(model))) * aNormal;
            TexCoord = aUV;
            gl_Position = projection * view * worldPos;
        }
    )";

    const char* cubeFS = R"(
        #version 330 core
        in vec3 FragPos;
        in vec3 Normal;
        in vec2 TexCoord;

        out vec4 FragColor;

        uniform sampler2D tex0;

        uniform vec3 lightPos;
        uniform vec3 viewPos;

        void main() {
            vec3 albedo = texture(tex0, TexCoord).rgb;

            vec3 norm = normalize(Normal);
            vec3 lightDir = normalize(lightPos - FragPos);

            // Ambient
            vec3 ambient = 0.20 * albedo;

            // Diffuse
            float diff = max(dot(norm, lightDir), 0.0);
            vec3 diffuse = diff * albedo;

            // Specular (simple Phong)
            vec3 viewDir = normalize(viewPos - FragPos);
            vec3 reflectDir = reflect(-lightDir, norm);
            float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32.0);
            vec3 specular = vec3(0.35) * spec;

            vec3 color = ambient + diffuse + specular;
            FragColor = vec4(color, 1.0);
        }
    )";

    // PLANET: emissive (looks like a light source)
    const char* planetVS = R"(
        #version 330 core
        layout (location = 0) in vec3 aPos;
        layout (location = 1) in vec3 aNormal;
        layout (location = 2) in vec2 aUV;

        uniform mat4 model;
        uniform mat4 view;
        uniform mat4 projection;

        void main() {
            gl_Position = projection * view * model * vec4(aPos, 1.0);
        }
    )";

    const char* planetFS = R"(
        #version 330 core
        out vec4 FragColor;
        uniform vec3 color;
        void main() {
            FragColor = vec4(color, 1.0);
        }
    )";

    GLuint cubeProg = makeProgram(cubeVS, cubeFS);
    GLuint planetProg = makeProgram(planetVS, planetFS);

    // ---------------- Texture for cubes ----------------
    // Αν το texture σου έχει άλλο όνομα/φάκελο, άλλαξε το path εδώ.
    GLuint cubeTex = loadTexture2D("assets/textures/container.jpg");
    if (cubeTex == 0) return 1;

    glUseProgram(cubeProg);
    glUniform1i(glGetUniformLocation(cubeProg, "tex0"), 0);

    // ---------------- Render loop ----------------
    while (!glfwWindowShouldClose(window)) {
        static float lastRealTime = (float)glfwGetTime();
        float realTime = (float)glfwGetTime();
        float dt = realTime - lastRealTime;
        lastRealTime = realTime;

        if (dt > 0.05f) dt = 0.05f;

        processInput(window, dt);

        glClearColor(0.08f, 0.09f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Pause-able time
        // static float lastRealTime = (float)glfwGetTime();
        // float realTime = (float)glfwGetTime();
        // float dt = realTime - lastRealTime;
        // lastRealTime = realTime;

        if (!gPaused) gSimTime += dt;
        float t = gSimTime;

        // Camera
        glm::vec3 camPos;
        camPos.x = gCamRadius * cosf(gPitch) * sinf(gYaw);
        camPos.y = gCamRadius * sinf(gPitch);
        camPos.z = gCamRadius * cosf(gPitch) * cosf(gYaw);

        glm::mat4 view = glm::lookAt(camPos, glm::vec3(0.0f), glm::vec3(0, 1, 0));

        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        float aspect = (h == 0) ? 1.0f : (float)w / (float)h;
        glm::mat4 projection = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);

        // Light is the planet position (center sphere)
        glm::vec3 lightPos = kPlanetPos;

        // ---------- Draw PLANET (OBJ) ----------
        glUseProgram(planetProg);

        glm::mat4 planetModel(1.0f);
        planetModel = glm::translate(planetModel, kPlanetPos);
        planetModel = glm::rotate(planetModel, kPlanetSpinW * t, glm::vec3(0, 1, 0)); // optional spin
        planetModel = glm::scale(planetModel, glm::vec3(kPlanetScale));

        glUniformMatrix4fv(glGetUniformLocation(planetProg, "model"), 1, GL_FALSE, glm::value_ptr(planetModel));
        glUniformMatrix4fv(glGetUniformLocation(planetProg, "view"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(planetProg, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
        glUniform3f(glGetUniformLocation(planetProg, "color"), 1.0f, 0.95f, 0.6f); // bright/emissive

        glBindVertexArray(planetMesh.VAO);
        glDrawArrays(GL_TRIANGLES, 0, planetMesh.vertexCount);
        glBindVertexArray(0);

        // ---------- Draw CUBES (textured + lit) ----------
        glUseProgram(cubeProg);
        glUniformMatrix4fv(glGetUniformLocation(cubeProg, "view"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(cubeProg, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
        glUniform3fv(glGetUniformLocation(cubeProg, "lightPos"), 1, glm::value_ptr(lightPos));
        glUniform3fv(glGetUniformLocation(cubeProg, "viewPos"), 1, glm::value_ptr(camPos));

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, cubeTex);

        glBindVertexArray(cubeVAO);

        for (int i = 0; i < kNumCubes; ++i) {
            float base = (2.0f * 3.1415926f) * (float)i / (float)kNumCubes;

            // orbit around planet
            float ang = base + kCubeOrbitW * t;

            glm::vec3 cubePos = kPlanetPos + glm::vec3(
                kCubeOrbitR * cosf(ang),
                0.0f,
                kCubeOrbitR * sinf(ang)
            );

            // different self-rotation per cube (explicitly different)
            float selfW = 0.9f + 0.35f * (i + 1); // 1.25,1.60,1.95,2.30,2.65,3.00

            glm::mat4 cubeModel(1.0f);
            cubeModel = glm::translate(cubeModel, cubePos);
            cubeModel = glm::rotate(cubeModel, selfW * t, glm::vec3(0.4f, 1.0f, 0.2f));
            cubeModel = glm::scale(cubeModel, glm::vec3(kCubeScale));

            glUniformMatrix4fv(glGetUniformLocation(cubeProg, "model"), 1, GL_FALSE, glm::value_ptr(cubeModel));
            glDrawArrays(GL_TRIANGLES, 0, 36);
        }

        glBindVertexArray(0);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // Cleanup
    glDeleteTextures(1, &cubeTex);

    glDeleteVertexArrays(1, &cubeVAO);
    glDeleteBuffers(1, &cubeVBO);

    glDeleteVertexArrays(1, &planetMesh.VAO);
    glDeleteBuffers(1, &planetMesh.VBO);

    glDeleteProgram(cubeProg);
    glDeleteProgram(planetProg);

    glfwTerminate();
    return 0;
}
