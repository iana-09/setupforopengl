// flappy_gl.cpp
// Flappy Bird-like game using GLFW + GLAD + OpenGL 3.3 core
// Single-file example
// Features:
//  - Start / Reset / Exit textured UI buttons (PNG)
//  - Click Start to begin; flap manually using SPACE
//  - Score increments when passing pipes; score shown live in window title
//  - Mouse + keyboard controls (SPACE to flap, ENTER to start, R to reset, ESC to exit)

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <vector>

using Clock = std::chrono::high_resolution_clock;

// Simple color shader (bird + pipes)
const char* vertexSrc = R"glsl(
#version 330 core
layout(location=0) in vec2 aPos;
uniform vec2 uPos;
uniform vec2 uScale;
void main() {
    vec2 pos = aPos * uScale + uPos;
    gl_Position = vec4(pos,0,1);
}
)glsl";

const char* fragSrc = R"glsl(
#version 330 core
out vec4 FragColor;
uniform vec3 uColor;
void main(){ FragColor = vec4(uColor,1.0); }
)glsl";

// Textured shader for UI
const char* texV = R"glsl(
#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
out vec2 vUV;
uniform vec2 uPos;
uniform vec2 uScale;
void main() {
    vUV = aUV;
    vec2 pos = aPos * uScale + uPos;
    gl_Position = vec4(pos,0,1);
}
)glsl";

const char* texF = R"glsl(
#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uTex;
uniform float uAlpha;
void main() {
    FragColor = texture(uTex,vUV);
    FragColor.a *= uAlpha;
}
)glsl";

// Helper: compile/link
GLuint compileShader(GLenum t, const char* src) {
    GLuint s = glCreateShader(t);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    int ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char buf[512]; glGetShaderInfoLog(s, 512, nullptr, buf); std::cerr << "Shader error: " << buf << "\n"; }
    return s;
}

GLuint linkProgram(const char* vs, const char* fs) {
    GLuint v = compileShader(GL_VERTEX_SHADER, vs);
    GLuint f = compileShader(GL_FRAGMENT_SHADER, fs);
    GLuint p = glCreateProgram();
    glAttachShader(p, v); glAttachShader(p, f);
    glLinkProgram(p);
    int ok; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char buf[512]; glGetProgramInfoLog(p, 512, nullptr, buf); std::cerr << "Link error: " << buf << "\n"; }
    glDeleteShader(v); glDeleteShader(f);
    return p;
}

// Rect verts for color shader
float rectVerts[] = { -0.5f,-0.5f,  0.5f,-0.5f,  0.5f,0.5f,  -0.5f,-0.5f,  0.5f,0.5f,  -0.5f,0.5f };

struct Pipe { float x; float gapY; float width; float gapSize; bool scored = false; };
struct UIButton { float x, y, w, h; GLuint tex = 0; bool visible = true; std::function<void()> onClick; };

// stb_image for PNG
#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

int main() {
    const int WIN_W = 1280, WIN_H = 720;
    if (!glfwInit()) { std::cerr << "GLFW init failed\n"; return -1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* win = glfwCreateWindow(WIN_W, WIN_H, "Hop Hop Bunny", nullptr, nullptr);
    if (!win) { std::cerr << "Window create failed\n"; glfwTerminate(); return -1; }
    glfwMakeContextCurrent(win);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) { std::cerr << "GLAD init failed\n"; return -1; }

    GLuint prog = linkProgram(vertexSrc, fragSrc);
    GLint locPos = glGetUniformLocation(prog, "uPos");
    GLint locScale = glGetUniformLocation(prog, "uScale");
    GLint locColor = glGetUniformLocation(prog, "uColor");

    GLuint vao, vbo; glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo);
    glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(rectVerts), rectVerts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glBindVertexArray(0);

    // textured UI program
    GLuint texProg = linkProgram(texV, texF);
    GLint texLocPos = glGetUniformLocation(texProg, "uPos");
    GLint texLocScale = glGetUniformLocation(texProg, "uScale");
    GLint texLocTex = glGetUniformLocation(texProg, "uTex");
    GLint texLocAlpha = glGetUniformLocation(texProg, "uAlpha");

    float quad[] = { -0.5f,-0.5f,0.0f,0.0f,  0.5f,-0.5f,1.0f,0.0f,  0.5f,0.5f,1.0f,1.0f,  -0.5f,-0.5f,0.0f,0.0f,  0.5f,0.5f,1.0f,1.0f,  -0.5f,0.5f,0.0f,1.0f };
    GLuint vaoTex, vboTex; glGenVertexArrays(1, &vaoTex); glGenBuffers(1, &vboTex);
    glBindVertexArray(vaoTex); glBindBuffer(GL_ARRAY_BUFFER, vboTex); glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glBindVertexArray(0);

    auto loadTex = [&](const char* path)->GLuint {
        int tw, th, tc; stbi_set_flip_vertically_on_load(1);
        unsigned char* d = stbi_load(path, &tw, &th, &tc, 4);
        if (!d) { std::cerr << "Failed load: " << path << "\n"; return 0; }
        GLuint t; glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tw, th, 0, GL_RGBA, GL_UNSIGNED_BYTE, d);
        glBindTexture(GL_TEXTURE_2D, 0); stbi_image_free(d); return t;
        };

    // buttons (940x788)
    UIButton startBtn, resetBtn, exitBtn;
    float scale = WIN_H / 2.0f / 788.0f; // fit half window height

    startBtn.w = 940 * scale; startBtn.h = 788 * scale;
    startBtn.x = WIN_W / 2.0f; startBtn.y = WIN_H / 2.0f; startBtn.visible = true;

    exitBtn.w = 940 * scale; exitBtn.h = 788 * scale;
    exitBtn.x = WIN_W / 2.0f; exitBtn.y = WIN_H / 2.0f + 200; // below Start
    exitBtn.visible = true;

    resetBtn.w = 940 * scale; resetBtn.h = 788 * scale;
    resetBtn.x = WIN_W / 2.0f; resetBtn.y = WIN_H / 2.0f; resetBtn.visible = false;

    startBtn.tex = loadTex("buttons/START button.png");
    resetBtn.tex = loadTex("buttons/RESET button.png");
    exitBtn.tex = loadTex("buttons/EXIT button.png");

    // game state
    float birdX = -0.4f, birdY = 0.0f, birdRadius = 0.045f, velocity = 0.0f;
    float collisionRadius = birdRadius * 0.75f;  // 25% smaller hitbox
    const float gravity = -1.5f, flapImpulse = 0.45f;
    std::vector<Pipe> pipes;
    const float pipeSpeed = 0.6f, spawnInterval = 1.6f; float timeSinceSpawn = 0.0f;
    int score = 0; bool gameStarted = false, gameRunning = false, gameOver = false;
    bool firstFlap = false; // new flag

    double mouseX = 0, mouseY = 0; bool mouseJustPressed = false;
    bool clickFlag = false; glfwSetWindowUserPointer(win, &clickFlag);
    glfwSetMouseButtonCallback(win, [](GLFWwindow* w, int button, int action, int mods) {
        if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
            bool* p = (bool*)glfwGetWindowUserPointer(w);
            if (p) *p = true;
        }
        });

    auto pixelToNDC = [&](float px, float py, int fbw, int fbh) {
        float nx = (px / (float)fbw) * 2.0f - 1.0f;
        float ny = 1.0f - (py / (float)fbh) * 2.0f;
        return std::pair<float, float>(nx, ny);
        };

    auto insideButton = [&](const UIButton& b, double mx, double my) {
        float left = b.x - b.w / 2.0f, right = b.x + b.w / 2.0f;
        float top = b.y - b.h / 2.0f, bottom = b.y + b.h / 2.0f;
        return (mx >= left && mx <= right && my >= top && my <= bottom);
        };

    // button callbacks
    startBtn.onClick = [&]() {
        birdY = 0.0f; velocity = 0.0f; pipes.clear(); timeSinceSpawn = 0; score = 0;
        gameStarted = true; gameRunning = true; gameOver = false;
        firstFlap = false; // reset
        startBtn.visible = false; exitBtn.visible = false; resetBtn.visible = false;
        glfwSetWindowTitle(win, "Flappy GL - Score: 0");
        };

    resetBtn.onClick = [&]() {
        birdY = 0.0f; velocity = 0.0f; pipes.clear(); timeSinceSpawn = 0; score = 0;
        gameStarted = true; gameRunning = true; gameOver = false;
        firstFlap = false; // reset
        resetBtn.visible = false; exitBtn.visible = false; startBtn.visible = false;
        glfwSetWindowTitle(win, "Flappy GL - Score: 0");
        };

    exitBtn.onClick = [&]() { glfwSetWindowShouldClose(win, 1); };

    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    auto now = Clock::now(); auto last = now;
    while (!glfwWindowShouldClose(win)) {
        now = Clock::now(); float dt = std::chrono::duration<float>(now - last).count(); if (dt > 0.05f) dt = 0.05f; last = now;
        int fbw, fbh; glfwGetFramebufferSize(win, &fbw, &fbh);
        glfwPollEvents();

        if (&clickFlag) { if (clickFlag) { glfwGetCursorPos(win, &mouseX, &mouseY); mouseJustPressed = true; clickFlag = false; } }

        // keyboard
        if (gameRunning && !gameOver) {
            static bool spacePrev = false;
            if (glfwGetKey(win, GLFW_KEY_SPACE) == GLFW_PRESS) {
                if (!spacePrev) {
                    velocity = flapImpulse;
                    firstFlap = true;
                }
                spacePrev = true;
            }
            else spacePrev = false;
        }
        if (!gameStarted && glfwGetKey(win, GLFW_KEY_ENTER) == GLFW_PRESS) startBtn.onClick();
        if (gameOver && glfwGetKey(win, GLFW_KEY_R) == GLFW_PRESS) resetBtn.onClick();
        if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS) glfwSetWindowShouldClose(win, 1);

        // mouse clicks
        if (mouseJustPressed) {
            if (insideButton(startBtn, mouseX, mouseY)) startBtn.onClick();
            else if (insideButton(resetBtn, mouseX, mouseY)) resetBtn.onClick();
            else if (insideButton(exitBtn, mouseX, mouseY)) exitBtn.onClick();
            mouseJustPressed = false;
        }

        // game update
        if (gameRunning && !gameOver) {
            if (firstFlap) velocity += gravity * dt;
            birdY += velocity * dt;

            timeSinceSpawn += dt;
            if (timeSinceSpawn > spawnInterval) {
                timeSinceSpawn = 0.0f; Pipe p;
                p.x = 1.2f; p.width = 0.18f; p.gapSize = 0.36f;
                p.gapY = -0.4f + ((float)rand() / RAND_MAX) * 0.8f; p.scored = false;
                pipes.push_back(p);
            }

            for (auto& p : pipes) p.x -= pipeSpeed * dt;

            // scoring
            for (auto& p : pipes) {
                if (!p.scored && (p.x + p.width / 2.0f) < birdX) {
                    p.scored = true; score++;
                    char buf[64]; snprintf(buf, sizeof(buf), "Flappy GL - Score: %d", score);
                    glfwSetWindowTitle(win, buf);
                }
            }

            // remove off-screen
            while (!pipes.empty() && pipes.front().x + pipes.front().width < -1.5f) pipes.erase(pipes.begin());

            // collisions
            for (auto& p : pipes) {
                float pipeLeft = p.x - p.width / 2.0f, pipeRight = p.x + p.width / 2.0f;
                float gapTop = p.gapY + p.gapSize / 2.0f, gapBottom = p.gapY - p.gapSize / 2.0f;
                if (birdX + collisionRadius > pipeLeft && birdX - collisionRadius < pipeRight) {
                    if (birdY + collisionRadius > gapTop || birdY - collisionRadius < gapBottom) {
                        gameOver = true; gameRunning = false;
                        resetBtn.visible = true; exitBtn.visible = true; startBtn.visible = false;
                    }
                }
            }
            if (birdY - birdRadius < -1.0f || birdY + birdRadius>1.0f) {
                gameOver = true; gameRunning = false;
                resetBtn.visible = true; exitBtn.visible = true; startBtn.visible = false;
            }
        }

        // render
        glViewport(0, 0, fbw, fbh);
        glClearColor(0.53f, 0.81f, 0.92f, 1.0f); glClear(GL_COLOR_BUFFER_BIT);

        // bird
        glUseProgram(prog); glBindVertexArray(vao);
        glUniform3f(locColor, 1.0f, 0.9f, 0.2f);
        glUniform2f(locPos, birdX, birdY);
        glUniform2f(locScale, birdRadius * 2.0f * ((float)fbh / fbw), birdRadius * 2.0f);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        // pipes
        for (auto& p : pipes) {
            float gapTop = p.gapY + p.gapSize / 2.0f; float topCenter = (gapTop + 1.0f) / 2.0f; float topScaleY = 1.0f - gapTop;
            glUniform3f(locColor, 0.18f, 0.8f, 0.17f); glUniform2f(locPos, p.x, topCenter); glUniform2f(locScale, p.width * ((float)fbh / fbw), topScaleY);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            float gapBottom = p.gapY - p.gapSize / 2.0f; float bottomCenter = (gapBottom - 1.0f) / 2.0f; float bottomScaleY = gapBottom - (-1.0f);
            glUniform2f(locPos, p.x, bottomCenter); glUniform2f(locScale, p.width * ((float)fbh / fbw), bottomScaleY);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }

        // UI
        glUseProgram(texProg); glBindVertexArray(vaoTex); glActiveTexture(GL_TEXTURE0); glUniform1i(texLocTex, 0);
        auto drawButton = [&](const UIButton& b) {
            if (!b.visible || b.tex == 0) return;
            glBindTexture(GL_TEXTURE_2D, b.tex);
            auto ndc = pixelToNDC(b.x, b.y, fbw, fbh);
            float sx = b.w / (float)fbw * 2.0f;
            float sy = b.h / (float)fbh * 2.0f;
            glUniform2f(texLocPos, ndc.first, ndc.second);
            glUniform2f(texLocScale, sx, sy); glUniform1f(texLocAlpha, 1.0f);
            glDrawArrays(GL_TRIANGLES, 0, 6); glBindTexture(GL_TEXTURE_2D, 0);
            };
        drawButton(startBtn); drawButton(exitBtn); drawButton(resetBtn);

        glBindVertexArray(0); glfwSwapBuffers(win);
    }

    glDeleteProgram(prog); glDeleteProgram(texProg);
    glDeleteBuffers(1, &vbo); glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &vboTex); glDeleteVertexArrays(1, &vaoTex);
    glfwDestroyWindow(win); glfwTerminate();
    return 0;
}
