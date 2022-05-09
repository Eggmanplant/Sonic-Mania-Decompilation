#ifdef STFU_INTELLISENSE
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "GL3RenderDevice.hpp"
#endif

GLFWwindow *RenderDevice::window;
GLuint RenderDevice::VAO;
GLuint RenderDevice::VBO;

GLuint RenderDevice::screenTextures[SCREEN_MAX];
GLuint RenderDevice::imageTexture;

double RenderDevice::lastFrame;
double RenderDevice::targetFreq;

int RenderDevice::monitorIndex;

bool RenderDevice::Init()
{
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    if (!RSDK::videoSettings.bordered)
        glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);

    GLFWmonitor *monitor = NULL;
    int w, h;
    if (RSDK::videoSettings.windowed) {
        w = RSDK::videoSettings.windowWidth;
        h = RSDK::videoSettings.windowHeight;
    }
    else if (RSDK::videoSettings.fsWidth <= 0 || RSDK::videoSettings.fsHeight <= 0) {
        monitor                 = glfwGetPrimaryMonitor();
        const GLFWvidmode *mode = glfwGetVideoMode(monitor);
        w                       = mode->width;
        h                       = mode->height;
    }
    else {
        monitor = glfwGetPrimaryMonitor();
        w       = RSDK::videoSettings.fsWidth;
        h       = RSDK::videoSettings.fsHeight;
    }

    RenderDevice::window = glfwCreateWindow(w, h, RSDK::gameVerInfo.gameName, monitor, NULL);
    if (!RenderDevice::window) {
        PrintLog(PRINT_NORMAL, "ERROR: [GLFW] window creation failed");
        return false;
    }
    PrintLog(PRINT_NORMAL, "w: %d h: %d windowed: %d\n", w, h, RSDK::videoSettings.windowed);

    glfwSetKeyCallback(window, ProcessKeyEvent);
    glfwSetJoystickCallback(ProcessJoystickEvent);
    glfwSetMouseButtonCallback(window, ProcessMouseEvent);
    glfwSetWindowFocusCallback(window, ProcessFocusEvent);
    glfwSetWindowMaximizeCallback(window, ProcessMaximizeEvent);

    // TODO: icon, best way i can see we do it is stb_image
    if (!SetupRendering() || !InitAudioDevice())
        return false;

    InitInputDevices();
    return true;
}

bool RenderDevice::SetupRendering()
{
    glfwMakeContextCurrent(window);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        PrintLog(PRINT_NORMAL, "ERROR: failed to initialize GLAD");
        return false;
    }

    RenderDevice::GetDisplays();

    if (!InitGraphicsAPI() || !InitShaders())
        return false;

    int size  = RSDK::videoSettings.pixWidth >= SCREEN_YSIZE ? RSDK::videoSettings.pixWidth : SCREEN_YSIZE;
    scanlines = (ScanlineInfo *)malloc(size * sizeof(ScanlineInfo));
    memset(scanlines, 0, size * sizeof(ScanlineInfo));

    RSDK::videoSettings.windowState = WINDOWSTATE_ACTIVE;
    RSDK::videoSettings.dimMax      = 1.0;
    RSDK::videoSettings.dimPercent  = 1.0;

    return true;
}

void RenderDevice::GetDisplays()
{
    GLFWmonitor *monitor = glfwGetWindowMonitor(window);
    if (!monitor)
        monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode *displayMode = glfwGetVideoMode(monitor);
    int monitorCount;
    GLFWmonitor **monitors = glfwGetMonitors(&monitorCount);

    for (int m = 0; m < monitorCount; ++m) {
        const GLFWvidmode *vidMode = glfwGetVideoMode(monitors[m]);
        displayWidth[m]            = vidMode->width;
        displayHeight[m]           = vidMode->height;
        if (!memcmp(vidMode, displayMode, sizeof(GLFWvidmode))) {
            monitorIndex = m;
        }
    }

    const GLFWvidmode *displayModes = glfwGetVideoModes(monitor, &displayCount);
    if (displayInfo.displays)
        free(displayInfo.displays);

    displayInfo.displays        = (decltype(displayInfo.displays))malloc(sizeof(GLFWvidmode) * displayCount);
    int newDisplayCount         = 0;
    bool foundFullScreenDisplay = false;

    for (int d = 0; d < displayCount; ++d) {
        memcpy(&displayInfo.displays[newDisplayCount].internal, &displayModes[d], sizeof(GLFWvidmode));

        int refreshRate = displayInfo.displays[newDisplayCount].refresh_rate;
        if (refreshRate >= 59 && (refreshRate <= 60 || refreshRate >= 120) && displayInfo.displays[newDisplayCount].height >= (SCREEN_YSIZE * 2)) {
            if (d && refreshRate == 60 && displayInfo.displays[newDisplayCount - 1].refresh_rate == 59)
                --newDisplayCount;

            if (RSDK::videoSettings.fsWidth == displayInfo.displays[newDisplayCount].width
                && RSDK::videoSettings.fsHeight == displayInfo.displays[newDisplayCount].height)
                foundFullScreenDisplay = true;

            ++newDisplayCount;
        }
    }

    displayCount = newDisplayCount;
    if (!foundFullScreenDisplay) {
        RSDK::videoSettings.fsWidth     = 0;
        RSDK::videoSettings.fsHeight    = 0;
        RSDK::videoSettings.refreshRate = 60; // 0;
    }
}

bool RenderDevice::InitGraphicsAPI()
{
    glClearColor(0.0, 0.0, 0.0, 1.0);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_DITHER);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);

    // setup buffers
    glGenVertexArrays(1, &VAO);
    glBindVertexArray(VAO);
    glGenBuffers(1, &VBO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(RenderVertex) * (!RETRO_REV02 ? 24 : 60), NULL, GL_DYNAMIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(RenderVertex), 0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(RenderVertex), (void *)offsetof(RenderVertex, color));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(RenderVertex), (void *)offsetof(RenderVertex, tex));
    glEnableVertexAttribArray(2);

    if (RSDK::videoSettings.windowed || !RSDK::videoSettings.exclusiveFS) {
        if (RSDK::videoSettings.windowed) {
            viewSize.x = RSDK::videoSettings.windowWidth;
            viewSize.y = RSDK::videoSettings.windowHeight;
        }
        else {
            viewSize.x = displayWidth[monitorIndex];
            viewSize.y = displayHeight[monitorIndex];
        }
    }
    else {
        int32 bufferWidth  = RSDK::videoSettings.fsWidth;
        int32 bufferHeight = RSDK::videoSettings.fsWidth;
        if (RSDK::videoSettings.fsWidth <= 0 || RSDK::videoSettings.fsHeight <= 0) {
            bufferWidth  = displayWidth[monitorIndex];
            bufferHeight = displayHeight[monitorIndex];
        }
        viewSize.x = bufferWidth;
        viewSize.y = bufferHeight;
    }

    int32 maxPixHeight = 0;
    for (int32 s = 0; s < SCREEN_MAX; ++s) {
        if (RSDK::videoSettings.pixHeight > maxPixHeight)
            maxPixHeight = RSDK::videoSettings.pixHeight;

        screens[s].size.y = RSDK::videoSettings.pixHeight;

        float viewAspect  = viewSize.x / viewSize.y;
        int32 screenWidth = (int)((viewAspect * RSDK::videoSettings.pixHeight) + 3) & 0xFFFFFFFC;
        if (screenWidth < RSDK::videoSettings.pixWidth)
            screenWidth = RSDK::videoSettings.pixWidth;

        // if (screenWidth > 424)
        //     screenWidth = 424;

        memset(&screens[s].frameBuffer, 0, sizeof(screens[s].frameBuffer));
        SetScreenSize(s, screenWidth, screens[s].size.y);
    }

    pixelSize.x     = screens[0].size.x;
    pixelSize.y     = screens[0].size.y;
    float pixAspect = pixelSize.x / pixelSize.y;

    Vector2 viewportPos{};
    Vector2 lastViewSize;

    glfwGetWindowSize(window, &lastViewSize.x, &lastViewSize.y);
    Vector2 viewportSize = lastViewSize;

    if ((viewSize.x / viewSize.y) <= ((pixelSize.x / pixelSize.y) + 0.1)) {
        if ((pixAspect - 0.1) > (viewSize.x / viewSize.y)) {
            viewSize.y     = (pixelSize.y / pixelSize.x) * viewSize.x;
            viewportPos.y  = (lastViewSize.y >> 1) - (viewSize.y * 0.5);
            viewportSize.y = viewSize.y;
        }
    }
    else {
        viewSize.x     = pixAspect * viewSize.y;
        viewportPos.x  = (lastViewSize.x >> 1) - ((pixAspect * viewSize.y) * 0.5);
        viewportSize.x = (pixAspect * viewSize.y);
    }

    if (maxPixHeight <= 256) {
        textureSize.x = 512.0;
        textureSize.y = 256.0;
    }
    else {
        textureSize.x = 1024.0;
        textureSize.y = 512.0;
    }

    glViewport(viewportPos.x, viewportPos.y, viewportSize.x, viewportSize.y);

    glActiveTexture(GL_TEXTURE0);
    glGenTextures(SCREEN_MAX, screenTextures);

    for (int i = 0; i < SCREEN_MAX; ++i) {
        glBindTexture(GL_TEXTURE_2D, screenTextures[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, textureSize.x, textureSize.y, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, NULL);

        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glGenTextures(1, &imageTexture);
    glBindTexture(GL_TEXTURE_2D, imageTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 1024, 512, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);

    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    lastShaderID = -1;
    InitVertexBuffer();
    RSDK::videoSettings.viewportX = viewportPos.x;
    RSDK::videoSettings.viewportY = viewportPos.y;
    RSDK::videoSettings.viewportW = 1.0 / viewSize.x;
    RSDK::videoSettings.viewportH = 1.0 / viewSize.y;

    return true;
}

void RenderDevice::InitVertexBuffer()
{
    // clang-format off
#if RETRO_REV02
RenderVertex vertBuffer[60] = {
    // 1 Screen (0)
    { { -1.0,  1.0,  1.0 }, 0xFFFFFFFF, {  0.0,  0.0 } },
    { { -1.0, -1.0,  1.0 }, 0xFFFFFFFF, {  0.0,  0.9375 } },
    { {  1.0, -1.0,  1.0 }, 0xFFFFFFFF, {  0.625,  0.9375 } },
    { { -1.0,  1.0,  1.0 }, 0xFFFFFFFF, {  0.0,  0.0 } },
    { {  1.0,  1.0,  1.0 }, 0xFFFFFFFF, {  0.625,  0.0 } },
    { {  1.0, -1.0,  1.0 }, 0xFFFFFFFF, {  0.625,  0.9375 } },
    
    // 2 Screens - Bordered (Top Screen) (6)
    { { -0.5,  1.0,  1.0 }, 0xFFFFFFFF, {  0.0,  0.0 } },
    { { -0.5,  0.0,  1.0 }, 0xFFFFFFFF, {  0.0,  0.9375 } },
    { {  0.5,  0.0,  1.0 }, 0xFFFFFFFF, {  0.625,  0.9375 } },
    { { -0.5,  1.0,  1.0 }, 0xFFFFFFFF, {  0.0,  0.0 } },
    { {  0.5,  1.0,  1.0 }, 0xFFFFFFFF, {  0.625,  0.0 } },
    { {  0.5,  0.0,  1.0 }, 0xFFFFFFFF, {  0.625,  0.9375 } },
    
    // 2 Screens - Bordered (Bottom Screen) (12)
    { { -0.5,  0.0,  1.0 }, 0xFFFFFFFF, {  0.0,  0.0 } },
    { { -0.5, -1.0,  1.0 }, 0xFFFFFFFF, {  0.0,  0.9375 } },
    { {  0.5, -1.0,  1.0 }, 0xFFFFFFFF, {  0.625,  0.9375 } },
    { { -0.5,  0.0,  1.0 }, 0xFFFFFFFF, {  0.0,  0.0 } },
    { {  0.5,  0.0,  1.0 }, 0xFFFFFFFF, {  0.625,  0.0 } },
    { {  0.5, -1.0,  1.0 }, 0xFFFFFFFF, {  0.625,  0.9375 } },
    
    // 2 Screens - Stretched (Top Screen)  (18)
    { { -1.0,  1.0,  1.0 }, 0xFFFFFFFF, {  0.0,  0.0 } },
    { { -1.0,  0.0,  1.0 }, 0xFFFFFFFF, {  0.0,  0.9375 } },
    { {  1.0,  0.0,  1.0 }, 0xFFFFFFFF, {  0.625,  0.9375 } },
    { { -1.0,  1.0,  1.0 }, 0xFFFFFFFF, {  0.0,  0.0 } },
    { {  1.0,  1.0,  1.0 }, 0xFFFFFFFF, {  0.625,  0.0 } },
    { {  1.0,  0.0,  1.0 }, 0xFFFFFFFF, {  0.625,  0.9375 } },
    
    // 2 Screens - Stretched (Bottom Screen) (24)
    { { -1.0,  0.0,  1.0 }, 0xFFFFFFFF, {  0.0,  0.0 } },
    { { -1.0, -1.0,  1.0 }, 0xFFFFFFFF, {  0.0,  0.9375 } },
    { {  1.0, -1.0,  1.0 }, 0xFFFFFFFF, {  0.625,  0.9375 } },
    { { -1.0,  0.0,  1.0 }, 0xFFFFFFFF, {  0.0,  0.0 } },
    { {  1.0,  0.0,  1.0 }, 0xFFFFFFFF, {  0.625,  0.0 } },
    { {  1.0, -1.0,  1.0 }, 0xFFFFFFFF, {  0.625,  0.9375 } },
    
    // 4 Screens (Top-Left) (30)
    { { -1.0,  1.0,  1.0 }, 0xFFFFFFFF, {  0.0,  0.0 } },
    { { -1.0,  0.0,  1.0 }, 0xFFFFFFFF, {  0.0,  0.9375 } },
    { {  0.0,  0.0,  1.0 }, 0xFFFFFFFF, {  0.625,  0.9375 } },
    { { -1.0,  1.0,  1.0 }, 0xFFFFFFFF, {  0.0,  0.0 } },
    { {  0.0,  1.0,  1.0 }, 0xFFFFFFFF, {  0.625,  0.0 } },
    { {  0.0,  0.0,  1.0 }, 0xFFFFFFFF, {  0.625,  0.9375 } },
    
    // 4 Screens (Top-Right) (36)
    { {  0.0,  1.0,  1.0 }, 0xFFFFFFFF, {  0.0,  0.0 } },
    { {  0.0,  0.0,  1.0 }, 0xFFFFFFFF, {  0.0,  0.9375 } },
    { {  1.0,  0.0,  1.0 }, 0xFFFFFFFF, {  0.625,  0.9375 } },
    { {  0.0,  1.0,  1.0 }, 0xFFFFFFFF, {  0.0,  0.0 } },
    { {  1.0,  1.0,  1.0 }, 0xFFFFFFFF, {  0.625,  0.0 } },
    { {  1.0,  0.0,  1.0 }, 0xFFFFFFFF, {  0.625,  0.9375 } },
    
    // 4 Screens (Bottom-Right) (48)
    { { -1.0,  0.0,  1.0 }, 0xFFFFFFFF, {  0.0,  0.0 } },
    { { -1.0, -1.0,  1.0 }, 0xFFFFFFFF, {  0.0,  0.9375 } },
    { {  0.0, -1.0,  1.0 }, 0xFFFFFFFF, {  0.625,  0.9375 } },
    { { -1.0,  0.0,  1.0 }, 0xFFFFFFFF, {  0.0,  0.0 } },
    { {  0.0,  0.0,  1.0 }, 0xFFFFFFFF, {  0.625,  0.0 } },
    { {  0.0, -1.0,  1.0 }, 0xFFFFFFFF, {  0.625,  0.9375 } },
    
    // 4 Screens (Bottom-Left) (42)
    { {  0.0,  0.0,  1.0 }, 0xFFFFFFFF, {  0.0,  0.0 } },
    { {  0.0, -1.0,  1.0 }, 0xFFFFFFFF, {  0.0,  0.9375 } },
    { {  1.0, -1.0,  1.0 }, 0xFFFFFFFF, {  0.625,  0.9375 } },
    { {  0.0,  0.0,  1.0 }, 0xFFFFFFFF, {  0.0,  0.0 } },
    { {  1.0,  0.0,  1.0 }, 0xFFFFFFFF, {  0.625,  0.0 } },
    { {  1.0, -1.0,  1.0 }, 0xFFFFFFFF, {  0.625,  0.9375 } },
    
    // Image/Video (54)
    { { -1.0,  1.0,  1.0 }, 0xFFFFFFFF, {  0.0,  0.0 } },
    { { -1.0, -1.0,  1.0 }, 0xFFFFFFFF, {  0.0,  1.0 } },
    { {  1.0, -1.0,  1.0 }, 0xFFFFFFFF, {  1.0,  1.0 } },
    { { -1.0,  1.0,  1.0 }, 0xFFFFFFFF, {  0.0,  0.0 } },
    { {  1.0,  1.0,  1.0 }, 0xFFFFFFFF, {  1.0,  0.0 } },
    { {  1.0, -1.0,  1.0 }, 0xFFFFFFFF, {  1.0,  1.0 } }
};
#else
RenderVertex vertBuffer[24] =
{
    // 1 Screen (0)
    { { -1.0,  1.0,  1.0 }, 0xFFFFFFFF, {  0.0,  0.0 } },
    { { -1.0, -1.0,  1.0 }, 0xFFFFFFFF, {  0.0,  0.9375 } },
    { {  1.0, -1.0,  1.0 }, 0xFFFFFFFF, {  0.625,  0.9375 } },
    { { -1.0,  1.0,  1.0 }, 0xFFFFFFFF, {  0.0,  0.0 } },
    { {  1.0,  1.0,  1.0 }, 0xFFFFFFFF, {  0.625,  0.0 } },
    { {  1.0, -1.0,  1.0 }, 0xFFFFFFFF, {  0.625,  0.9375 } },

  // 2 Screens - Stretched (Top Screen) (6)
    { { -1.0,  1.0,  1.0 }, 0xFFFFFFFF, {  0.0,  0.0 } },
    { { -1.0,  0.0,  1.0 }, 0xFFFFFFFF, {  0.0,  0.9375 } },
    { {  1.0,  0.0,  1.0 }, 0xFFFFFFFF, {  0.625,  0.9375 } },
    { { -1.0,  1.0,  1.0 }, 0xFFFFFFFF, {  0.0,  0.0 } },
    { {  1.0,  1.0,  1.0 }, 0xFFFFFFFF, {  0.625,  0.0 } },
    { {  1.0,  0.0,  1.0 }, 0xFFFFFFFF, {  0.625,  0.9375 } },
  
  // 2 Screens - Stretched (Bottom Screen) (12)
    { { -1.0,  0.0,  1.0 }, 0xFFFFFFFF, {  0.0,  0.0 } },
    { { -1.0, -1.0,  1.0 }, 0xFFFFFFFF, {  0.0,  0.9375 } },
    { {  1.0, -1.0,  1.0 }, 0xFFFFFFFF, {  0.625,  0.9375 } },
    { { -1.0,  0.0,  1.0 }, 0xFFFFFFFF, {  0.0,  0.0 } },
    { {  1.0,  0.0,  1.0 }, 0xFFFFFFFF, {  0.625,  0.0 } },
    { {  1.0, -1.0,  1.0 }, 0xFFFFFFFF, {  0.625,  0.9375 } },
  
    // Image/Video (18)
    { { -1.0,  1.0,  1.0 }, 0xFFFFFFFF, {  0.0,  0.0 } },
    { { -1.0, -1.0,  1.0 }, 0xFFFFFFFF, {  0.0,  1.0 } },
    { {  1.0, -1.0,  1.0 }, 0xFFFFFFFF, {  1.0,  1.0 } },
    { { -1.0,  1.0,  1.0 }, 0xFFFFFFFF, {  0.0,  0.0 } },
    { {  1.0,  1.0,  1.0 }, 0xFFFFFFFF, {  1.0,  0.0 } },
    { {  1.0, -1.0,  1.0 }, 0xFFFFFFFF, {  1.0,  1.0 } }
};
#endif
    // clang-format on

    float x = 0.5 / (float)viewSize.x;
    float y = 0.5 / (float)viewSize.y;
    // float x = 0;
    // float y = 0;

    for (int v = 0; v < (!RETRO_REV02 ? 24 : 60); ++v) {
        RenderVertex *vertex = &vertBuffer[v];
        vertex->pos.x        = vertex->pos.x - x;
        vertex->pos.y        = vertex->pos.y + y;

        if (vertex->tex.x)
            vertex->tex.x = screens[0].size.x * (1.0 / textureSize.x);

        if (vertex->tex.y)
            vertex->tex.y = screens[0].size.y * (1.0 / textureSize.y);
    }
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(RenderVertex) * (!RETRO_REV02 ? 24 : 60), vertBuffer);
}

void RenderDevice::InitFPSCap()
{
    lastFrame  = glfwGetTime();
    targetFreq = 1.0 / RSDK::videoSettings.refreshRate;
}
bool RenderDevice::CheckFPSCap()
{
    if (lastFrame + targetFreq < glfwGetTime())
        return true;

    return false;
}
void RenderDevice::UpdateFPSCap() { lastFrame = glfwGetTime(); }

void RenderDevice::CopyFrameBuffer()
{
    for (int s = 0; s < RSDK::videoSettings.screenCount; ++s) {
        glBindTexture(GL_TEXTURE_2D, screenTextures[s]);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, screens[s].pitch, SCREEN_YSIZE, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, screens[s].frameBuffer);
    }
}

bool RenderDevice::ProcessEvents()
{
    glfwPollEvents();
    return !glfwWindowShouldClose(window);
}

void RenderDevice::FlipScreen()
{
    if (lastShaderID != RSDK::videoSettings.shaderID) {
        lastShaderID = RSDK::videoSettings.shaderID;

        SetLinear(shaderList[RSDK::videoSettings.shaderID].linear);

        glUseProgram(shaderList[RSDK::videoSettings.shaderID].programID);
    }

    if (RSDK::videoSettings.dimTimer < RSDK::videoSettings.dimLimit) {
        if (RSDK::videoSettings.dimPercent < 1.0) {
            RSDK::videoSettings.dimPercent += 0.05;
            if (RSDK::videoSettings.dimPercent > 1.0)
                RSDK::videoSettings.dimPercent = 1.0;
        }
    }
    else if (RSDK::videoSettings.dimPercent > 0.25) {
        RSDK::videoSettings.dimPercent *= 0.9;
    }

    if (windowRefreshDelay > 0) {
        windowRefreshDelay--;
        if (!windowRefreshDelay)
            UpdateGameWindow();
        return;
    }

    glClear(GL_COLOR_BUFFER_BIT);
    glUniform1f(glGetUniformLocation(shaderList[RSDK::videoSettings.shaderID].programID, "screenDim"),
                RSDK::videoSettings.dimMax * RSDK::videoSettings.dimPercent);

    int32 startVert = 0;
    switch (RSDK::videoSettings.screenCount) {
        default:
        case 0:
#if RETRO_REV02
            startVert = 54;
#else
            startVert = 18;
#endif
            glBindTexture(GL_TEXTURE_2D, imageTexture);
            glDrawArrays(GL_TRIANGLES, startVert, 6);

            break;

        case 1:
            glBindTexture(GL_TEXTURE_2D, screenTextures[0]);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            break;

        case 2:
#if RETRO_REV02
            startVert = startVertex_2P[0];
#else
            startVert = 6;
#endif
            glBindTexture(GL_TEXTURE_2D, screenTextures[0]);
            glDrawArrays(GL_TRIANGLES, startVert, 6);

#if RETRO_REV02
            startVert = startVertex_2P[1];
#else
            startVert = 12;
#endif
            glBindTexture(GL_TEXTURE_2D, screenTextures[1]);
            glDrawArrays(GL_TRIANGLES, startVert, 6);
            break;

#if RETRO_REV02
        case 3:
            glBindTexture(GL_TEXTURE_2D, screenTextures[0]);
            glDrawArrays(GL_TRIANGLES, startVertex_3P[0], 6);

            glBindTexture(GL_TEXTURE_2D, screenTextures[1]);
            glDrawArrays(GL_TRIANGLES, startVertex_3P[1], 6);

            glBindTexture(GL_TEXTURE_2D, screenTextures[2]);
            glDrawArrays(GL_TRIANGLES, startVertex_3P[2], 6);
            break;

        case 4:
            glBindTexture(GL_TEXTURE_2D, screenTextures[0]);
            glDrawArrays(GL_TRIANGLES, 30, 6);

            glBindTexture(GL_TEXTURE_2D, screenTextures[1]);
            glDrawArrays(GL_TRIANGLES, 36, 6);

            glBindTexture(GL_TEXTURE_2D, screenTextures[2]);
            glDrawArrays(GL_TRIANGLES, 42, 6);

            glBindTexture(GL_TEXTURE_2D, screenTextures[3]);
            glDrawArrays(GL_TRIANGLES, 48, 6);
            break;
#endif
    }

    glFlush();
    glfwSwapBuffers(window);
}

void RenderDevice::Release(bool32 isRefresh)
{
    if (imageTexture) {
        glDeleteTextures(1, &imageTexture);
    }

    glDeleteTextures(SCREEN_MAX, screenTextures);
    for (int i = 0; i < shaderCount; ++i) {
        glDeleteProgram(shaderList[i].programID);
    }
    shaderCount     = 0;
    userShaderCount = 0;

    glfwDestroyWindow(window);

    if (!isRefresh) {
        if (displayInfo.displays)
            free(displayInfo.displays);
        displayInfo.displays = NULL;

        if (scanlines)
            free(scanlines);
        scanlines = NULL;

        glDeleteVertexArrays(1, &VAO);
        glDeleteBuffers(1, &VBO);
        glfwTerminate();
    }
}

bool RenderDevice::InitShaders()
{
    RSDK::videoSettings.shaderSupport = true;
    int32 maxShaders                  = 0;
    if (RSDK::videoSettings.shaderSupport) {
        LoadShader("None", false);
        LoadShader("Clean", true);
        LoadShader("CRT-Yeetron", true);
        LoadShader("CRT-Yee64", true);

#if RETRO_USE_MOD_LOADER
        // a place for mods to load custom shaders
        RSDK::RunModCallbacks(RSDK::MODCB_ONSHADERLOAD, NULL);
        userShaderCount = shaderCount;
#endif

        LoadShader("YUV-420", true);
        LoadShader("YUV-422", true);
        LoadShader("YUV-444", true);
        LoadShader("RGB-Image", true);
        maxShaders = shaderCount;
    }
    else {
        for (int s = 0; s < SHADER_MAX; ++s) shaderList[s].linear = true;

        shaderList[0].linear = RSDK::videoSettings.windowed ? false : shaderList[0].linear;
        maxShaders           = 1;
        shaderCount          = 1;
    }

    RSDK::videoSettings.shaderID = RSDK::videoSettings.shaderID >= maxShaders ? 0 : RSDK::videoSettings.shaderID;

    SetLinear(shaderList[RSDK::videoSettings.shaderID].linear || RSDK::videoSettings.screenCount > 1);

    return true;
}
void RenderDevice::LoadShader(const char *fileName, bool32 linear)
{
    char buffer[0x100];
    FileInfo info;

    for (int i = 0; i < shaderCount; ++i) {
        if (strcmp(shaderList[i].name, fileName) == 0)
            return;
    }

    if (shaderCount == SHADER_MAX)
        return;

    ShaderEntry *shader = &shaderList[shaderCount];
    shader->linear      = linear;
    sprintf(shader->name, "%s", fileName);

    const char *shaderFolder    = "Dummy"; // nothing should ever be in "Data/Shaders/Dummy" so it works out to never load anything
    const char *vertexShaderExt = "txt";
    const char *pixelShaderExt  = "txt";

    const char *bytecodeFolder    = "CSO-Dummy"; // nothing should ever be in "Data/Shaders/CSO-Dummy" so it works out to never load anything
    const char *vertexBytecodeExt = "bin";
    const char *pixelBytecodeExt  = "bin";

    shaderFolder    = "GL3";
    vertexShaderExt = "vs";
    pixelShaderExt  = "fs";

    GLint success;
    char infoLog[0x1000];
    GLuint vert, frag;
    sprintf(buffer, "Data/Shaders/%s/%s.%s", shaderFolder, fileName, vertexShaderExt);
    InitFileInfo(&info);
    if (LoadFile(&info, buffer, FMODE_RB)) {
        byte *fileData = NULL;
        RSDK::AllocateStorage(info.fileSize + 1, (void **)&fileData, RSDK::DATASET_TMP, false);
        ReadBytes(&info, fileData, info.fileSize);
        fileData[info.fileSize] = 0;
        CloseFile(&info);

        const GLchar *glchar[] = { (const GLchar *)fileData };
        vert                   = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vert, 1, glchar, NULL);
        glCompileShader(vert);
    }
    else
        return;
    sprintf(buffer, "Data/Shaders/%s/%s.%s", shaderFolder, fileName, pixelShaderExt);
    InitFileInfo(&info);
    if (LoadFile(&info, buffer, FMODE_RB)) {
        byte *fileData = NULL;
        RSDK::AllocateStorage(info.fileSize + 1, (void **)&fileData, RSDK::DATASET_TMP, false);
        ReadBytes(&info, fileData, info.fileSize);
        fileData[info.fileSize] = 0;
        CloseFile(&info);

        const GLchar *glchar[] = { (const GLchar *)fileData };
        frag                   = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(frag, 1, glchar, NULL);
        glCompileShader(frag);
    }
    else
        return;
    shader->programID = glCreateProgram();
    glAttachShader(shader->programID, vert);
    glAttachShader(shader->programID, frag);
    glLinkProgram(shader->programID);
    glGetProgramiv(shader->programID, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(shader->programID, 0x1000, NULL, infoLog);
        PrintLog(PRINT_ERROR, "OpenGL shader linking failed:\n%s", infoLog);
        return;
    }
    glDeleteShader(vert);
    glDeleteShader(frag);
    glBindAttribLocation(shader->programID, 0, "in_pos");
    glBindAttribLocation(shader->programID, 1, "in_color");
    glBindAttribLocation(shader->programID, 2, "in_UV");
    shaderCount++;
};

void RenderDevice::RefreshWindow()
{
    RSDK::videoSettings.windowState = WINDOWSTATE_UNINITIALIZED;

    RenderDevice::Release(true);
    if (!RSDK::videoSettings.bordered)
        glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);

    GLFWmonitor *monitor = NULL;
    int w, h;
    if (RSDK::videoSettings.windowed) {
        w = RSDK::videoSettings.windowWidth;
        h = RSDK::videoSettings.windowHeight;
    }
    else if (RSDK::videoSettings.fsWidth <= 0 || RSDK::videoSettings.fsHeight <= 0) {
        monitor                 = glfwGetPrimaryMonitor();
        const GLFWvidmode *mode = glfwGetVideoMode(monitor);
        w                       = mode->width;
        h                       = mode->height;
    }
    else {
        monitor = glfwGetPrimaryMonitor();
        w       = RSDK::videoSettings.fsWidth;
        h       = RSDK::videoSettings.fsHeight;
    }

    RenderDevice::window = glfwCreateWindow(w, h, RSDK::gameVerInfo.gameName, monitor, NULL);
    if (!RenderDevice::window) {
        PrintLog(PRINT_NORMAL, "ERROR: [GLFW] window creation failed");
        return;
    }
    PrintLog(PRINT_NORMAL, "w: %d h: %d windowed: %d\n", w, h, RSDK::videoSettings.windowed);

    glfwSetKeyCallback(window, ProcessKeyEvent);
    glfwSetMouseButtonCallback(window, ProcessMouseEvent);
    glfwSetWindowFocusCallback(window, ProcessFocusEvent);
    glfwSetWindowMaximizeCallback(window, ProcessMaximizeEvent);

    glfwMakeContextCurrent(window);

    if (!InitGraphicsAPI() || !InitShaders())
        return;
}
void RenderDevice::SetupImageTexture(int32 width, int32 height, uint8 *imagePixels)
{
    if (imagePixels) {
        glBindTexture(GL_TEXTURE_2D, imageTexture);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, imagePixels);
    }
}

void RenderDevice::ProcessKeyEvent(GLFWwindow *, int key, int scancode, int action, int mods)
{
    switch (action) {
        case GLFW_PRESS: {
#if !RETRO_REV02
            ++buttonDownCount;
#endif
            switch (key) {
                case GLFW_KEY_ENTER:
                    if (mods & GLFW_MOD_ALT) {
                        RSDK::videoSettings.windowed ^= 1;
                        UpdateGameWindow();
                        RSDK::changedVideoSettings = false;
                        break;
                    }
                    // [fallthrough]

                default:
#if RETRO_INPUTDEVICE_KEYBOARD
                    UpdateKeyState(key);
#endif
                    break;

                case GLFW_KEY_ESCAPE:
                    if (engine.devMenu) {
                        if (sceneInfo.state == ENGINESTATE_DEVMENU)
                            CloseDevMenu();
                        else
                            OpenDevMenu();
                    }
                    else {
#if RETRO_INPUTDEVICE_KEYBOARD
                        UpdateKeyState(key);
#endif
                    }

#if !RETRO_REV02 && RETRO_INPUTDEVICE_KEYBOARD
                    specialKeyStates[0] = true;
#endif
                    break;

#if !RETRO_REV02 && RETRO_INPUTDEVICE_KEYBOARD
                case GLFW_KEY_ENTER: specialKeyStates[1] = true; break;
#endif

#if !RETRO_USE_ORIGINAL_CODE
                case GLFW_KEY_F1:
                    sceneInfo.listPos--;
                    if (sceneInfo.listPos < sceneInfo.listCategory[sceneInfo.activeCategory].sceneOffsetStart) {
                        sceneInfo.activeCategory--;
                        if (sceneInfo.activeCategory >= sceneInfo.categoryCount) {
                            sceneInfo.activeCategory = sceneInfo.categoryCount - 1;
                        }
                        sceneInfo.listPos = sceneInfo.listCategory[sceneInfo.activeCategory].sceneOffsetEnd - 1;
                    }

                    InitSceneLoad();
                    break;

                case GLFW_KEY_F2:
                    sceneInfo.listPos++;
                    if (sceneInfo.listPos >= sceneInfo.listCategory[sceneInfo.activeCategory].sceneOffsetEnd) {
                        sceneInfo.activeCategory++;
                        if (sceneInfo.activeCategory >= sceneInfo.categoryCount) {
                            sceneInfo.activeCategory = 0;
                        }
                        sceneInfo.listPos = sceneInfo.listCategory[sceneInfo.activeCategory].sceneOffsetStart;
                    }

                    InitSceneLoad();
                    break;
#endif

                case GLFW_KEY_F3:
                    if (userShaderCount)
                        RSDK::videoSettings.shaderID = (RSDK::videoSettings.shaderID + 1) % userShaderCount;
                    break;

#if !RETRO_USE_ORIGINAL_CODE
                case GLFW_KEY_F5:
                    // Quick-Reload
                    InitSceneLoad();
                    break;

                case GLFW_KEY_F6:
                    if (engine.devMenu && RSDK::videoSettings.screenCount > 1)
                        RSDK::videoSettings.screenCount--;
                    break;

                case GLFW_KEY_F7:
                    if (engine.devMenu && RSDK::videoSettings.screenCount < SCREEN_MAX)
                        RSDK::videoSettings.screenCount++;
                    break;

                case GLFW_KEY_F9:
                    if (engine.devMenu)
                        showHitboxes ^= 1;
                    break;

                case GLFW_KEY_F10:
                    if (engine.devMenu)
                        engine.showPaletteOverlay ^= 1;
                    break;
#endif
                case GLFW_KEY_BACKSPACE:
                    if (engine.devMenu)
                        engine.gameSpeed = engine.fastForwardSpeed;
                    break;

                case GLFW_KEY_F11:
                case GLFW_KEY_INSERT:
                    if ((sceneInfo.state & ENGINESTATE_STEPOVER) == ENGINESTATE_STEPOVER)
                        engine.frameStep = true;
                    break;

                case GLFW_KEY_F12:
                case GLFW_KEY_PAUSE:
                    if (engine.devMenu) {
                        sceneInfo.state ^= ENGINESTATE_STEPOVER;
                    }
                    break;
            }
            break;
        }
        case GLFW_RELEASE: {
#if !RETRO_REV02
            --buttonDownCount;
#endif
            switch (key) {
                default:
#if RETRO_INPUTDEVICE_KEYBOARD
                    ClearKeyState(key);
#endif
                    break;
#if !RETRO_REV02 && RETRO_INPUTDEVICE_KEYBOARD
                case GLFW_KEY_ESCAPE: specialKeyStates[0] = false; break;
                case GLFW_KEY_ENTER: specialKeyStates[1] = false; break;
#endif
                case GLFW_KEY_BACKSPACE: engine.gameSpeed = 1; break;
            }
            break;
        }
    }
}
void RenderDevice::ProcessFocusEvent(GLFWwindow *, int focused)
{
    if (!focused) {
#if RETRO_REV02
        RSDK::SKU::userCore->focusState = 1;
#endif
    }
    else {
#if RETRO_REV02
        RSDK::SKU::userCore->focusState = 0;
#endif
    }
}
void RenderDevice::ProcessMouseEvent(GLFWwindow *, int button, int action, int mods)
{
    switch (action) {
        case GLFW_PRESS: {
            switch (button) {
                case GLFW_MOUSE_BUTTON_LEFT: touchMouseData.down[0] = true; touchMouseData.count = 1;
#if !RETRO_REV02
                    if (buttonDownCount > 0)
                        buttonDownCount--;
#endif
                    break;

                case GLFW_MOUSE_BUTTON_RIGHT:
#if !RETRO_REV02 && RETRO_INPUTDEVICE_KEYBOARD
                    specialKeyStates[3] = true;
                    buttonDownCount++;
#endif
                    break;
            }
            break;
        }
        case GLFW_RELEASE: {
            switch (button) {
                case GLFW_MOUSE_BUTTON_LEFT: touchMouseData.down[0] = false; touchMouseData.count = 0;
#if !RETRO_REV02
                    if (buttonDownCount > 0)
                        buttonDownCount--;
#endif
                    break;

                case GLFW_MOUSE_BUTTON_RIGHT:
#if !RETRO_REV02 && RETRO_INPUTDEVICE_KEYBOARD
                    specialKeyStates[3] = false;
                    buttonDownCount--;
#endif
                    break;
            }
            break;
        }
    }
}
void RenderDevice::ProcessJoystickEvent(int ID, int event) {}
void RenderDevice::ProcessMaximizeEvent(GLFWwindow *, int maximized)
{
    // i don't know why this is a thing
    if (maximized) {
        // set fullscreen idk about the specifics rn
    }
}

void RenderDevice::SetLinear(bool32 linear)
{
    for (int i = 0; i < SCREEN_MAX; ++i) {
        glBindTexture(GL_TEXTURE_2D, screenTextures[i]);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    }
}