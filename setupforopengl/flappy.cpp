// flappy_gl.cpp
// Hop Hop Bunny - Final Polish
// Fixed: "Best Score" text is now responsive (scales with screen) and much larger.

#include <windows.h>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")

using Clock = std::chrono::high_resolution_clock;

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

static float clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

GLuint compileShader(GLenum t, const char* src) {
    GLuint s = glCreateShader(t);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    int ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char buf[1024]; glGetShaderInfoLog(s, 1024, nullptr, buf); std::cerr << "Shader error: " << buf << "\n"; }
    return s;
}

GLuint linkProgram(const char* vs, const char* fs) {
    GLuint v = compileShader(GL_VERTEX_SHADER, vs);
    GLuint f = compileShader(GL_FRAGMENT_SHADER, fs);
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    int ok; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char buf[1024]; glGetProgramInfoLog(p, 1024, nullptr, buf); std::cerr << "Link error: " << buf << "\n"; }
    glDeleteShader(v); glDeleteShader(f);
    return p;
}

// Quad data (NDC unit quad centered at origin)
float rectVerts[] = { -0.5f,-0.5f, 0.5f,-0.5f, 0.5f,0.5f, -0.5f,-0.5f, 0.5f,0.5f, -0.5f,0.5f };
float quad[] = {
    -0.5f,-0.5f, 0.0f,0.0f, 0.5f,-0.5f, 1.0f,0.0f, 0.5f,0.5f,1.0f,1.0f,
    -0.5f,-0.5f,0.0f,0.0f, 0.5f,0.5f,1.0f,1.0f, -0.5f,0.5f,0.0f,1.0f
};

struct Pipe { float x; float gapY; float width; float gapSize; bool scored = false; };
struct UIButton { float x, y, w, h; GLuint tex = 0; bool visible = true; std::function<void()> onClick; };
struct Cloud { float x_px, y_px, speed; GLuint tex; float w_px, h_px; };

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

int main() {
    const int WIN_W = 1280, WIN_H = 720;
    if (!glfwInit()) { std::cerr << "GLFW init failed\n"; return -1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* win = glfwCreateWindow(WIN_W, WIN_H, "Bunny Hop Adventure", nullptr, nullptr);
    if (!win) { std::cerr << "Window create failed\n"; glfwTerminate(); return -1; }
    glfwMakeContextCurrent(win);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) { std::cerr << "GLAD init failed\n"; return -1; }

    // Play lobby music (looping)
    PlaySound(TEXT("lobby.wav"), nullptr, SND_FILENAME | SND_ASYNC | SND_LOOP);


    // Programs
    GLuint prog = linkProgram(vertexSrc, fragSrc);
    GLint locPos = glGetUniformLocation(prog, "uPos");
    GLint locScale = glGetUniformLocation(prog, "uScale");
    GLint locColor = glGetUniformLocation(prog, "uColor");

    GLuint vao, vbo; glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo);
    glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(rectVerts), rectVerts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glBindVertexArray(0);

    GLuint texProg = linkProgram(texV, texF);
    GLint texLocPos = glGetUniformLocation(texProg, "uPos");
    GLint texLocScale = glGetUniformLocation(texProg, "uScale");
    GLint texLocTex = glGetUniformLocation(texProg, "uTex");
    GLint texLocAlpha = glGetUniformLocation(texProg, "uAlpha");

    GLuint vaoTex, vboTex; glGenVertexArrays(1, &vaoTex); glGenBuffers(1, &vboTex);
    glBindVertexArray(vaoTex); glBindBuffer(GL_ARRAY_BUFFER, vboTex);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glBindVertexArray(0);

    auto loadTex = [&](const char* path, int* out_w = nullptr, int* out_h = nullptr)->GLuint {
        int tw = 0, th = 0, tc = 0;
        stbi_set_flip_vertically_on_load(1);
        unsigned char* d = stbi_load(path, &tw, &th, &tc, 4);
        if (!d) { std::cerr << "Failed load: " << path << "\n"; return 0; }
        GLuint t; glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tw, th, 0, GL_RGBA, GL_UNSIGNED_BYTE, d);
        glBindTexture(GL_TEXTURE_2D, 0);
        stbi_image_free(d);
        if (out_w)*out_w = tw; if (out_h)*out_h = th;
        return t;
        };

    // Buttons & textures
    UIButton startBtn, resetBtn, exitBtn;

    // Original Dimensions
    const float BTN_W = 940.0f, BTN_H = 788.0f;

    startBtn.visible = true;
    exitBtn.visible = true;
    resetBtn.visible = false;

    // Load textures
    startBtn.tex = loadTex("buttons/START button.png");
    resetBtn.tex = loadTex("buttons/RESET button.png");
    exitBtn.tex = loadTex("buttons/EXIT button.png");
    if (!exitBtn.tex) std::cerr << "Failed to load EXIT button texture\n";

    GLuint bunnyTexIdle = loadTex("bunny sequence/bunny_sequence 1.png");
    GLuint bunnyTexFlap = loadTex("bunny sequence/bunny_sequence 2.png");
    GLuint bunnyTexDied = loadTex("bunny sequence/bunny died.png");

    GLuint cloudTex1 = loadTex("clouds/cloud1.png");
    GLuint cloudTex2 = loadTex("clouds/cloud2.png");

    GLuint grassTex = loadTex("ground/grass.png");

    GLuint numberTex[10];
    for (int i = 0; i < 10; i++) {
        char path[64];
        snprintf(path, sizeof(path), "numbers/%d.png", i);
        numberTex[i] = loadTex(path);
    }
    GLuint textGameTitle = loadTex("text/game title.png");
    GLuint textGameOver = loadTex("text/game over.png");
    GLuint textBestScore = loadTex("text/best score.png");

    GLuint bestScoreTex[10];
    for (int i = 0; i < 10; i++) {
        char path[64];
        snprintf(path, sizeof(path), "bestscores/%d.png", i);
        bestScoreTex[i] = loadTex(path);
    }

    // Game state
    float birdX = -0.4f, birdY = 0.0f;
    const float birdRadius = 0.012f;
    std::vector<Pipe> pipes;
    const float pipeSpeed = 0.3f, spawnInterval = 1.6f;
    const float cloudSpeed = pipeSpeed * WIN_W * 0.5f;
    float timeSinceSpawn = 0.0f;
    int score = 0;
    int bestScore = 0;
    bool gameStarted = false, gameOver = false;
    float bunnyAnimTimer = 0.0f; const float bunnyAnimDuration = 0.2f;
    int bunnyFrame = 0;
    bool firstFlapDone = false;

    double mouseX = 0, mouseY = 0; bool mouseJustPressed = false, clickFlag = false;
    glfwSetWindowUserPointer(win, &clickFlag);
    glfwSetMouseButtonCallback(win, [](GLFWwindow* w, int button, int action, int mods) {
        if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
            bool* p = (bool*)glfwGetWindowUserPointer(w);
            if (p) *p = true;
        }
        });

    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    auto pixelToNDC = [&](float px, float py, int fbw, int fbh) {
        return std::pair<float, float>((px / fbw) * 2.0f - 1.0f, 1.0f - (py / fbh) * 2.0f);
        };

    auto drawTexPixel = [&](GLuint tex, float cx, float cy, float w, float h, int fbw, int fbh, float alpha = 1.0f) {
        if (!tex) return;
        glBindTexture(GL_TEXTURE_2D, tex);
        auto ndc = pixelToNDC(cx, cy, fbw, fbh);
        float sx = (w / (float)fbw) * 2.0f, sy = (h / (float)fbh) * 2.0f;
        glUseProgram(texProg);
        glBindVertexArray(vaoTex);
        glActiveTexture(GL_TEXTURE0);
        glUniform1i(texLocTex, 0);
        glUniform2f(texLocPos, ndc.first, ndc.second);
        glUniform2f(texLocScale, sx, sy);
        glUniform1f(texLocAlpha, alpha);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindTexture(GL_TEXTURE_2D, 0);
        };

    auto drawButton = [&](const UIButton& b, int fbw, int fbh) {
        if (b.visible && b.tex) drawTexPixel(b.tex, b.x, b.y, b.w, b.h, fbw, fbh);
        };

    // CLOUDS 
    std::vector<Cloud> clouds;
    struct CloudParams { float xMul, yMul, wScale, hScale; };
    CloudParams cloudParams[] = {
        {0.2f, 0.15f, 0.5f, 0.4f},
        {0.7f, 0.22f, 0.4f, 0.3f},
        {0.4f, 0.10f, 0.35f, 0.25f},
        {0.85f, 0.18f, 0.45f, 0.35f}
    };

    GLuint cloudTexs[] = { cloudTex1, cloudTex2, cloudTex1, cloudTex2 };

    clouds.clear();
    for (int i = 0; i < 4; i++) {
        if (cloudTexs[i]) {
            float w_px = 940.0f * cloudParams[i].wScale;
            float h_px = 788.0f * cloudParams[i].hScale;
            float x_px = WIN_W * cloudParams[i].xMul;
            float y_px = WIN_H * cloudParams[i].yMul;
            clouds.push_back({ x_px, y_px, cloudSpeed, cloudTexs[i], w_px, h_px });
        }
    }

    // Button callbacks
    startBtn.onClick = [&]() {
        birdY = 0.0f; pipes.clear(); timeSinceSpawn = 0; score = 0;
        gameStarted = true; gameOver = false; firstFlapDone = false;
        startBtn.visible = false; resetBtn.visible = false;
        exitBtn.visible = false;
        char buf[128]; snprintf(buf, sizeof(buf), "Bunny Hop Adventure - Score: %d", score); glfwSetWindowTitle(win, buf);
        };
    resetBtn.onClick = [&]() {
        birdY = 0.0f; pipes.clear(); timeSinceSpawn = 0; score = 0;
        gameStarted = false; gameOver = false; firstFlapDone = false;
        startBtn.visible = true; exitBtn.visible = true; resetBtn.visible = false;
        char buf[128];
        snprintf(buf, sizeof(buf), "Bunny Hop Adventure - Best: %d", bestScore);
        glfwSetWindowTitle(win, buf);
        };
    exitBtn.onClick = [&]() { glfwSetWindowShouldClose(win, 1); };

    auto now = Clock::now(); auto last = now;
    auto startTime = Clock::now();

    const float pipeWidth = 0.12f;
    const float pipeGapSize = 0.50f;
    const float flapStrength = 0.60f;
    const float gravity = -2.30f;
    float birdVel = 0.0f;

    srand((unsigned int)time(nullptr));

    // drawScore - UPDATED TO BE RESPONSIVE
    std::function<void(int, int, int, bool)> drawScore;
    drawScore = [&](int scoreVal, int fbw, int fbh, bool isGameOver)
        {
            float elapsed = std::chrono::duration<float>(Clock::now() - startTime).count();

            // current score (gameplay only)
            if (!isGameOver && gameStarted) {
                std::vector<int> digits;
                if (scoreVal == 0) digits.push_back(0);
                else {
                    int tmp = scoreVal;
                    while (tmp > 0) { digits.push_back(tmp % 10); tmp /= 10; }
                    std::reverse(digits.begin(), digits.end());
                }

                float numW = 80.0f, numH = 70.0f;
                float totalW = numW * digits.size();
                float x = (fbw - totalW) * 0.5f + numW * 0.5f;
                float y = fbh * 0.03f + numH * 0.5f;

                for (int i = 0; i < (int)digits.size(); i++)
                    drawTexPixel(numberTex[digits[i]], x + i * numW, y, numW, numH, fbw, fbh);
            }

            // game title
            if (!gameStarted && !isGameOver) {
                float bob = sinf(elapsed * 2.0f) * 6.0f;
                float scale = 0.92f + 0.06f * sinf(elapsed * 1.8f);
                float baseW = fbw * 0.56f;
                float titleW = baseW * scale;
                float titleH = titleW * (788.0f / 940.0f);
                float titleX = fbw * 0.5f;
                float titleY = fbh * 0.18f + bob;
                drawTexPixel(textGameTitle, titleX, titleY, titleW, titleH, fbw, fbh, 1.0f);
            }

            // game over text
            if (isGameOver) {
                float goW = fbw * 0.5f;
                float goH = goW * (788.0f / 940.0f);
                float goX = fbw * 0.5f;
                float goY = fbh * 0.28f;
                drawTexPixel(textGameOver, goX, goY, goW, goH, fbw, fbh, 1.0f);
            }

            // best score (game over only) -- FIXED SIZE
            if (isGameOver)
            {
                std::vector<int> bestDigits;
                if (bestScore == 0) bestDigits.push_back(0);
                else {
                    int tmp = bestScore;
                    while (tmp > 0) { bestDigits.push_back(tmp % 10); tmp /= 10; }
                    std::reverse(bestDigits.begin(), bestDigits.end());
                }

                // Calculate scale based on current window height
                // 720.0f is our reference height. If window is 1440p, text doubles in size.
                float uiScale = fbh / 720.0f;

                // Base dimensions increased and multiplied by scale
                float labelW = 550.0f * uiScale;
                float labelH = 180.0f * uiScale;
                float digitW = 140.0f * uiScale;
                float digitH = 110.0f * uiScale;

                // Overall size multiplier if needed

                float spacing = 20.0f * uiScale;

                float numbersWidth = digitW * bestDigits.size();
                float totalWidth = labelW + spacing + numbersWidth;
                float centerY = fbh * 0.40f; // Positioned between Game Over and Reset
                float labelX = (fbw - totalWidth) * 0.5f + labelW * 0.5f;
                float numbersStartX = labelX + labelW * 0.5f + spacing + digitW * 0.5f;

                drawTexPixel(textBestScore, labelX, centerY, labelW, labelH, fbw, fbh, 0.95f);
                for (int i = 0; i < (int)bestDigits.size(); i++) {
                    float dx = numbersStartX + i * digitW;
                    drawTexPixel(bestScoreTex[bestDigits[i]], dx, centerY, digitW, digitH, fbw, fbh, 1.0f);
                }
            }
        };

    // Main loop
    while (!glfwWindowShouldClose(win)) {
        now = Clock::now();
        float dt = std::chrono::duration<float>(now - last).count();
        if (dt > 0.05f) dt = 0.05f;
        last = now;

        int fbw, fbh; glfwGetFramebufferSize(win, &fbw, &fbh);

        // --- RESPONSIVE UI UPDATE ---
        float btnScale = (fbh * 0.28f) / BTN_H;

        startBtn.w = resetBtn.w = exitBtn.w = BTN_W * btnScale;
        startBtn.h = resetBtn.h = exitBtn.h = BTN_H * btnScale;

        startBtn.x = fbw * 0.5f;
        startBtn.y = fbh * 0.38f + startBtn.h * 0.25f;

        float exitGap = startBtn.h * 0.65f;
        exitBtn.x = fbw * 0.5f;
        exitBtn.y = startBtn.y + exitGap;

        resetBtn.x = fbw * 0.5f;
        resetBtn.y = fbh * 0.5f;
        // -----------------------------

        glfwPollEvents();

        if (clickFlag) { glfwGetCursorPos(win, &mouseX, &mouseY); mouseJustPressed = true; clickFlag = false; }

        static bool spacePrev = false;
        bool spaceNow = (glfwGetKey(win, GLFW_KEY_SPACE) == GLFW_PRESS);

        // Mouse click hop or button clicks
        if (mouseJustPressed) {
            if (startBtn.visible &&
                (mouseX >= startBtn.x - startBtn.w / 2 && mouseX <= startBtn.x + startBtn.w / 2 &&
                    mouseY >= startBtn.y - startBtn.h / 2 && mouseY <= startBtn.y + startBtn.h / 2)) {
                startBtn.onClick();
            }
            else if (resetBtn.visible &&
                (mouseX >= resetBtn.x - resetBtn.w / 2 && mouseX <= resetBtn.x + resetBtn.w / 2 &&
                    mouseY >= resetBtn.y - resetBtn.h / 2 && mouseY <= resetBtn.y + resetBtn.h / 2)) {
                resetBtn.onClick();
            }
            else if (exitBtn.visible &&
                (mouseX >= exitBtn.x - exitBtn.w / 2 && mouseX <= exitBtn.x + exitBtn.w / 2 &&
                    mouseY >= exitBtn.y - exitBtn.h / 2 && mouseY <= exitBtn.y + exitBtn.h / 2)) {
                exitBtn.onClick();
            }
            else if (gameStarted && !gameOver) {
                birdVel = +flapStrength;
                firstFlapDone = true;
                PlaySound(TEXT("hop.wav"), nullptr, SND_FILENAME | SND_ASYNC);
            }
            mouseJustPressed = false;
        }

        if (gameStarted && !gameOver && spaceNow && !spacePrev) {
            birdVel = +flapStrength;
            firstFlapDone = true;
            PlaySound(TEXT("hop.wav"), nullptr, SND_FILENAME | SND_ASYNC);
        }
        spacePrev = spaceNow;

        if (gameStarted && firstFlapDone) {
            birdVel += gravity * dt;
            birdY += birdVel * dt;
        }

        if (birdY + birdRadius > 1.0f) {
            birdY = 1.0f - birdRadius;
            birdVel = 0;
        }

        if (birdY - birdRadius < -1.0f) {
            birdY = -1.0f + birdRadius;
            gameOver = true;
            resetBtn.visible = true;
            exitBtn.visible = true;
        }

        if (!gameStarted) {
            birdY = 0.0f;
            birdVel = 0.0f;
            exitBtn.visible = true;
            startBtn.visible = true;
            resetBtn.visible = false;
        }

        if (gameStarted && !gameOver) {
            timeSinceSpawn += dt;
            if (timeSinceSpawn > spawnInterval) {
                timeSinceSpawn = 0.0f;
                Pipe p;
                p.x = 1.2f;
                p.width = pipeWidth;
                p.gapSize = pipeGapSize;
                float margin = 0.2f;
                float halfGap = p.gapSize * 0.5f;
                p.gapY = -1.0f + margin + halfGap + ((float)rand() / RAND_MAX) * (2.0f - 2.0f * margin - p.gapSize);
                p.scored = false;
                pipes.push_back(p);
            }
        }

        if (gameStarted && !gameOver) {
            for (auto& p : pipes) p.x -= pipeSpeed * dt;
        }

        for (auto& p : pipes) {
            if (!p.scored && p.x + p.width * 0.5f < birdX) {
                p.scored = true;
                score++;
                if (score > bestScore) bestScore = score;
                char buf[128]; snprintf(buf, sizeof(buf), "Bunny Hop Adventure - Score: %d  Best: %d", score, bestScore);
                glfwSetWindowTitle(win, buf);
            }
        }

        while (!pipes.empty() && pipes.front().x + pipes.front().width < -1.5f) pipes.erase(pipes.begin());

        float aspect = (float)fbw / (float)fbh;
        for (auto& p : pipes) {
            float pl = p.x - p.width * 0.5f;
            float pr = p.x + p.width * 0.5f;
            float gt = p.gapY + p.gapSize * 0.5f;
            float gb = p.gapY - p.gapSize * 0.5f;

            float scaledBirdLeft = (birdX - birdRadius) * aspect;
            float scaledBirdRight = (birdX + birdRadius) * aspect;
            float scaledPipeLeft = pl * aspect;
            float scaledPipeRight = pr * aspect;

            bool overlapsX = !(scaledBirdRight < scaledPipeLeft || scaledBirdLeft > scaledPipeRight);
            bool insideGap = (birdY + birdRadius < gt) && (birdY - birdRadius > gb);

            if (overlapsX && !insideGap) {
                gameOver = true;
                resetBtn.visible = true;
                exitBtn.visible = true;
                break;
            }
        }

        for (auto& c : clouds) {
            if (!gameOver) {
                c.x_px -= c.speed * dt;
                if (c.x_px + c.w_px < 0) c.x_px = WIN_W + 10.0f;
            }
        }

        if (!gameOver) {
            bunnyAnimTimer += dt;
            if (bunnyAnimTimer >= bunnyAnimDuration) {
                bunnyAnimTimer = 0.0f;
                bunnyFrame = (bunnyFrame + 1) % 2;
            }
        }

        glViewport(0, 0, fbw, fbh);
        glClearColor(0.53f, 0.81f, 0.92f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        if (grassTex) {
            const float grassOrigAspect = 940.0f / 788.0f;
            float grassHeight = fbh * 0.12f;
            float grassWidth = grassHeight * grassOrigAspect;
            float grassY = fbh - grassHeight * 0.5f;
            int numTiles = (int)ceilf((float)fbw / grassWidth) + 1;
            for (int i = 0; i < numTiles; i++) {
                float grassX = i * grassWidth + grassWidth * 0.5f;
                drawTexPixel(grassTex, grassX, grassY, grassWidth, grassHeight, fbw, fbh, 1.0f);
            }
        }

        for (auto& c : clouds) drawTexPixel(c.tex, c.x_px + c.w_px * 0.5f, c.y_px + c.h_px * 0.5f, c.w_px, c.h_px, fbw, fbh, 0.95f);

        glUseProgram(prog);
        glBindVertexArray(vao);
        const float pipeR = 0.45f, pipeG = 0.8f, pipeB = 0.45f;

        for (auto& p : pipes) {
            float pl = p.x - p.width * 0.5f;
            float pr = p.x + p.width * 0.5f;
            float gt = p.gapY + p.gapSize * 0.5f;
            float gb = p.gapY - p.gapSize * 0.5f;

            float topHeight = 1.0f - gt;
            float topCenterY = gt + topHeight * 0.5f;
            glUniform3f(locColor, pipeR, pipeG, pipeB);
            glUniform2f(locPos, (pl + pr) * 0.5f, topCenterY);
            glUniform2f(locScale, p.width, topHeight);
            glDrawArrays(GL_TRIANGLES, 0, 6);

            float bottomHeight = gb + 1.0f;
            float bottomCenterY = -1.0f + bottomHeight * 0.5f;
            glUniform3f(locColor, pipeR * 0.92f, pipeG * 0.92f, pipeB * 0.92f);
            glUniform2f(locPos, (pl + pr) * 0.5f, bottomCenterY);
            glUniform2f(locScale, p.width, bottomHeight);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }

        glUseProgram(texProg);
        glBindVertexArray(vaoTex);
        GLuint currentBunnyTex = gameOver ? bunnyTexDied : (bunnyFrame == 0 ? bunnyTexIdle : bunnyTexFlap);
        float bunny_px_x = ((birdX + 1.0f) * 0.5f) * fbw;
        float bunny_px_y = ((1.0f - birdY) * 0.5f) * fbh;
        drawTexPixel(currentBunnyTex, bunny_px_x, bunny_px_y, 90, 90, fbw, fbh);

        drawScore(score, fbw, fbh, gameOver);
        drawButton(startBtn, fbw, fbh);
        drawButton(exitBtn, fbw, fbh);
        drawButton(resetBtn, fbw, fbh);

        glfwSwapBuffers(win);
    }

    glfwTerminate();
    return 0;
}
