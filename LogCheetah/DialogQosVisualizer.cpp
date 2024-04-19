// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

// These are based on CommonSchema's "correlation vector" concepts.  See also: https://github.com/microsoft/CorrelationVector

#include "DialogQosVisualizer.h"
#include "Globals.h"
#include "WinMain.h"
#include "GuiStatusMonitor.h"
#include "resource.h"
#include "DebugWindow.h"
#include "MainLogView.h"

#include <Windowsx.h>
#include <CommCtrl.h>
#include <thread>
#include <list>
#include <memory>
#include <fstream>

#pragma warning(disable:4251)
#include <glbinding/gl32core/gl.h> 
#include <glbinding/glbinding.h>
#include <glbinding-aux/debug.h>
#include <glbinding-aux/ContextInfo.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

using namespace std::chrono_literals;
using namespace std::string_literals;
using namespace gl;

namespace
{
    const int MsBufferBetweenRequestsOnLayer = 25;
    const double MsScrollPerSecond = 5.0f;
    const float FarClipPlane = 1000.0f;

    struct QosData
    {
        // Computed based on raw data in the post-filter pass
        uint64_t StartTimeMs = 0; //ms since the first request from all the data

        // Raw data directly from the logline
        uint64_t DurationMs = 0;
        bool Success = true;
        bool CallerFault = false;
        std::string EndTimestamp;
        std::string CorrelationFull;
        std::string CorrelationBase;
    };

    struct OutQosRequest: public QosData
    {
        //TODO: name information might go here
    };

    struct InQosRequest: public QosData
    {
        //TODO: name information might go here

        std::vector<OutQosRequest> AssociatedOutRequests; //ordered front to back
    };

    struct QosRenderLayer
    {
        std::vector<InQosRequest> Requests;
    };

    struct QosRenderData
    {
        std::vector<QosRenderLayer> Layers; //ordered front to back
        uint64_t MaxDataTimeMs = 0;
    };

    class DrawerHelper
    {
    public:
        std::string SetupFailure; // empty on success;
        double ScrollMultiplier = 1.0;

        float PickerHightlightScreenX = 0.0f;
        float PickerHightlightScreenY = 0.0f;

        float MsPerXUnit = 100.0f;

        DrawerHelper(HWND hwndWindow)
        {
            glGenBuffers(1, &vbBoxes);
            if (!vbBoxes)
            {
                SetupFailure = "Failed to create vertex buffer.  (Machine doesn't have a GPU?)";
                return;
            }

            vertShader = CompileShader("shaders\\vert.glsl", GL_VERTEX_SHADER);
            fragShader = CompileShader("shaders\\frag.glsl", GL_FRAGMENT_SHADER);
            bool shaderProgramSuccess = false;
            std::tie(shaderProgramSuccess, shaderProgram) = CreateProgram({ vertShader, fragShader });
            if (!shaderProgramSuccess)
                SetupFailure = "Failed to create shader program.";
        }

        ~DrawerHelper()
        {
            glDeleteBuffers(1, &vbBoxes);

            glDetachShader(shaderProgram, vertShader);
            glDetachShader(shaderProgram, fragShader);
            glDeleteShader(vertShader);
            glDeleteShader(fragShader);
            glDeleteProgram(shaderProgram);
        }

        void Draw(float aspectWidOverHei, const QosRenderData &renderData)
        {
            // Autoscroll
            auto nowTime = std::chrono::steady_clock::now();
            double timePassed = std::chrono::duration_cast<std::chrono::nanoseconds>(nowTime - lastUpdateTime).count() * 0.000000001;
            lastUpdateTime = nowTime;

            timeScrollInMs += timePassed * MsScrollPerSecond * ScrollMultiplier;

            if (timeScrollInMs < 0.0)
                timeScrollInMs = 0.0;
            else if (timeScrollInMs > renderData.MaxDataTimeMs * 1.0 / MsPerXUnit)
                timeScrollInMs = (double)renderData.MaxDataTimeMs * 1.0 / MsPerXUnit;

            // Camera and projection
            glm::mat4 matProj = glm::perspective(3.14f * 0.5f, aspectWidOverHei, 0.1f, FarClipPlane);
            glm::mat4 matView = glm::lookAt(
                glm::vec3(timeScrollInMs, -20, -5), //camera
                glm::vec3(timeScrollInMs, 0, 20), //at
                glm::vec3(0, -1, 0)); //up
            glm::mat4 matViewProj = matProj * matView;
            lastMatViewProj = matViewProj;

            // Picking pre-computation
            glm::vec3 pickerHightlightRayPos = ScreenToWorldSpace(glm::vec3(PickerHightlightScreenX, PickerHightlightScreenY, 0));
            glm::vec3 pickerHightlightRayDir = glm::normalize(ScreenToWorldSpace(glm::vec3(PickerHightlightScreenX, PickerHightlightScreenY, 1)) - pickerHightlightRayPos);
           
            // Oversimplified not truly valid clipping, compute bounds for which requests might actually even need rendered
            float viewMsWidth = aspectWidOverHei * MsPerXUnit * 250;
            int64_t clipStartMs = (int64_t)(timeScrollInMs * MsPerXUnit - viewMsWidth);
            int64_t clipEndMs = (int64_t)(timeScrollInMs * MsPerXUnit + viewMsWidth);

            auto shouldDraw = [&](const QosData &req)
            {
                if ((int64_t)(req.StartTimeMs + req.DurationMs) < clipStartMs)
                    return false;
                else if ((int64_t)req.StartTimeMs > clipEndMs)
                    return false;
                else
                    return true;
            };

            // Draw top level requests front to back
            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);

            glUseProgram(shaderProgram);
            GLuint programUniformViewProj = glGetUniformLocation(shaderProgram, "viewProj");
            glUniformMatrix4fv(programUniformViewProj, 1, GL_FALSE, &matViewProj[0][0]);

            glBindBuffer(GL_ARRAY_BUFFER, vbBoxes);

            const size_t floatsPerVertex = 9;
            const GLsizei vertexStride = floatsPerVertex * sizeof(float);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, vertexStride, (void*)0);
            glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, vertexStride, (void*)(3 * sizeof(float)));
            glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, vertexStride, (void*)(7 * sizeof(float)));
            glEnableVertexAttribArray(0);
            glEnableVertexAttribArray(1);
            glEnableVertexAttribArray(2);

            bool hasAlreadyHighlightedOne = false; // mouse click code picks the first one, so only highlight the first one

            for (size_t layerNumber = 0; layerNumber < renderData.Layers.size(); ++layerNumber)
            {
                const QosRenderLayer &layer = renderData.Layers[layerNumber];

                vertMemory.clear();
                for (const InQosRequest &req : layer.Requests)
                {
                    if (!shouldDraw(req))
                        continue;

                    bool highlight = false;
                    if (!hasAlreadyHighlightedOne && PickerHightlightScreenX != 0.0f && PickerHightlightScreenY != 0.0f)
                    {
                        highlight = TestRayIntersection(pickerHightlightRayPos, pickerHightlightRayDir, req, layerNumber);
                        hasAlreadyHighlightedOne = highlight;
                    }

                    AddInRequestBoxVerts(vertMemory, req, layerNumber, highlight);
                }

                glBufferData(GL_ARRAY_BUFFER, vertMemory.size() * sizeof(GLfloat), vertMemory.data(), GL_DYNAMIC_DRAW);

                GLsizei vertexCount = (GLsizei)vertMemory.size() / floatsPerVertex;
                glDrawArrays(GL_TRIANGLES, 0, vertexCount);
            }

            // Draw nested outgoing requests with transparency back to front
            glDepthMask(GL_FALSE);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            for (int64_t layerNumber = renderData.Layers.size() - 1; layerNumber >= 0; --layerNumber)
            {
                const QosRenderLayer &layer = renderData.Layers[layerNumber];

                vertMemory.clear();
                for (const InQosRequest &inReq : layer.Requests)
                {
                    if (!shouldDraw(inReq))
                        continue;

                    for (int64_t outIndex = inReq.AssociatedOutRequests.size() - 1; outIndex >= 0; --outIndex)
                    {
                        const OutQosRequest &outReq = inReq.AssociatedOutRequests[outIndex];
                        AddOutRequestDangleVerts(vertMemory, outReq, layerNumber, outIndex, inReq.AssociatedOutRequests.size());
                    }
                }

                glBufferData(GL_ARRAY_BUFFER, vertMemory.size() * sizeof(GLfloat), vertMemory.data(), GL_DYNAMIC_DRAW);

                GLsizei vertexCount = (GLsizei)vertMemory.size() / floatsPerVertex;
                glDrawArrays(GL_TRIANGLES, 0, vertexCount);
            }

            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);

            glDisableVertexAttribArray(0);
            glDisableVertexAttribArray(1);
            glDisableVertexAttribArray(2);
        }

        void SetScrollPositionMs(uint64_t timeInMs)
        {
            timeScrollInMs = (double)timeInMs *  1.0 / MsPerXUnit;
        }

        glm::vec3 ScreenToWorldSpace(const glm::vec3 &homogenousScreenPos)
        {
            glm::mat4 inverseViewProj = glm::inverse(lastMatViewProj);
            glm::vec4 unprojectedWorld = inverseViewProj * glm::vec4(homogenousScreenPos, 1.0f);
            return glm::vec3(unprojectedWorld) * (1.0f / unprojectedWorld.w);
        }

        const InQosRequest* IntersectWorldRayWithQosData(const glm::vec3& pos, const glm::vec3& dir, const QosRenderData &renderData)
        {
            glm::vec3 normalizedDir = glm::normalize(dir);

            // walk the layers front to back, first intersection wins
            for (size_t layerNumber = 0; layerNumber < renderData.Layers.size(); ++layerNumber)
            {
                const QosRenderLayer& layer = renderData.Layers[layerNumber];
                for (const InQosRequest& req : layer.Requests)
                {
                    if (TestRayIntersection(pos, normalizedDir, req, layerNumber))
                        return &req;
                }
            }

            return nullptr;
        }

    private:
        GLuint vbBoxes = 0;
        GLuint vertShader = 0;
        GLuint fragShader = 0;
        GLuint shaderProgram = 0;

        std::vector<GLfloat> vertMemory;

        double timeScrollInMs = 0.0;
        std::chrono::steady_clock::time_point lastUpdateTime = std::chrono::steady_clock::now();

        glm::mat4 lastMatViewProj;

        GLuint CompileShader(const std::string &filename, GLenum shaderType)
        {
            std::string shaderSource = LoadFile(filename);
            if (shaderSource.empty())
            {
                GlobalDebugOutput("Could not open shader file: " + filename);
                return 0;
            }

            GLuint shader = glCreateShader(shaderType);
            const char* shaderSourcePointer = shaderSource.c_str();
            glShaderSource(shader, 1, &shaderSourcePointer, nullptr);
            glCompileShader(shader);

            GLint compileResult = 0;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &compileResult);
            GLint compileLogLength = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &compileLogLength);
            std::string compileLog;
            compileLog.resize(compileLogLength);
            glGetShaderInfoLog(shader, compileLogLength, nullptr, compileLog.data());
            while (!compileLog.empty() && compileLog.back() == 0)
                compileLog.pop_back();

            GlobalDebugOutput("Shader '" + filename + "' compile result " + (compileResult ? "success" : "failure") + ": " + compileLog);
            return shader;
        }

        std::tuple<bool, GLuint> CreateProgram(std::vector<GLuint> shaders)
        {
            GLuint program = glCreateProgram();
            for (GLuint s : shaders)
                glAttachShader(program, s);
            glLinkProgram(program);

            GLint linkResult = 0;
            glGetProgramiv(program, GL_LINK_STATUS, &linkResult);
            GLint linkLogLength = 0;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &linkLogLength);
            std::string linkLog;
            linkLog.resize(linkLogLength);
            glGetProgramInfoLog(program, linkLogLength, nullptr, linkLog.data());
            while (!linkLog.empty() && linkLog.back() == 0)
                linkLog.pop_back();

            GlobalDebugOutput(std::string() + "Program link result " + (linkResult ? "success" : "failure") + ": " + linkLog);
            return { linkResult != 0, program };
        }

        std::string LoadFile(const std::string &filename)
        {
            std::ifstream file(filename);
            if (file.is_open())
            {
                std::streampos startPos = file.tellg();
                file.seekg(0, std::ios::end);
                std::streampos endPos = file.tellg();
                file.seekg(startPos, std::ios::beg);

                std::string s;
                s.reserve(endPos - startPos);
                s.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());

                return s;
            }

            return {};
        }

        void AddInRequestBoxVerts(std::vector<GLfloat> &verts, const InQosRequest &req, size_t layer, bool highlight)
        {
            // local model coords
            float xStart = req.StartTimeMs * (1 / MsPerXUnit);
            float xEnd = (req.StartTimeMs + req.DurationMs) * (1 / MsPerXUnit);

            float yTop = 0.0f;
            float yBottom = 1.0f;

            float zNear = 0.0f;
            float zFar = 1.0f;

            // other properties
            float yOffset = -1.0f;
            float zOffset = layer * 3.0f;

            float r, g, b;
            if (req.CallerFault)
            {
                r = 0.6f; g = 0.6f; b = 0.0f;
            }
            else if (req.Success)
            {
                r = 0.0f; g = 1.0f; b = 0.0f;
            }
            else
            {
                r = 1.0f; g = 0.0f; b = 0.0f;
            }

            if (highlight)
            {
                r += 0.7f; g += 0.7f; b += 0.7f;
            }

            float a = 1.0f;

            // front (don't need a back)
            verts.insert(verts.end(), { xStart, yTop,    zNear, r, g, b, a, yOffset, zOffset });
            verts.insert(verts.end(), { xEnd,   yTop,    zNear, r, g, b, a, yOffset, zOffset });
            verts.insert(verts.end(), { xStart, yBottom, zNear, r, g, b, a, yOffset, zOffset });

            verts.insert(verts.end(), { xStart, yBottom, zNear, r, g, b, a, yOffset, zOffset });
            verts.insert(verts.end(), { xEnd,   yTop,    zNear, r, g, b, a, yOffset, zOffset });
            verts.insert(verts.end(), { xEnd,   yBottom, zNear, r, g, b, a, yOffset, zOffset });

            //top (don't need bottom)
            verts.insert(verts.end(), { xStart, yTop,    zNear, r, g, b, a, yOffset, zOffset });
            verts.insert(verts.end(), { xEnd,   yTop,    zNear, r, g, b, a, yOffset, zOffset });
            verts.insert(verts.end(), { xStart, yTop,    zFar,  r, g, b, a, yOffset, zOffset });

            verts.insert(verts.end(), { xStart, yTop,    zFar,  r, g, b, a, yOffset, zOffset });
            verts.insert(verts.end(), { xEnd,   yTop,    zNear, r, g, b, a, yOffset, zOffset });
            verts.insert(verts.end(), { xEnd,   yTop,    zFar,  r, g, b, a, yOffset, zOffset });

            // left and right
            verts.insert(verts.end(), { xStart, yTop,    zNear, r, g, b, a, yOffset, zOffset });
            verts.insert(verts.end(), { xStart, yTop,    zFar,  r, g, b, a, yOffset, zOffset });
            verts.insert(verts.end(), { xStart, yBottom, zNear, r, g, b, a, yOffset, zOffset });

            verts.insert(verts.end(), { xStart, yBottom, zNear, r, g, b, a, yOffset, zOffset });
            verts.insert(verts.end(), { xStart, yTop,    zFar,  r, g, b, a, yOffset, zOffset });
            verts.insert(verts.end(), { xStart, yBottom, zFar,  r, g, b, a, yOffset, zOffset });

            verts.insert(verts.end(), { xEnd,   yTop,    zNear, r, g, b, a, yOffset, zOffset });
            verts.insert(verts.end(), { xEnd,   yTop,    zFar,  r, g, b, a, yOffset, zOffset });
            verts.insert(verts.end(), { xEnd,   yBottom, zNear, r, g, b, a, yOffset, zOffset });

            verts.insert(verts.end(), { xEnd,   yBottom, zNear, r, g, b, a, yOffset, zOffset });
            verts.insert(verts.end(), { xEnd,   yTop,    zFar,  r, g, b, a, yOffset, zOffset });
            verts.insert(verts.end(), { xEnd,   yBottom, zFar,  r, g, b, a, yOffset, zOffset });
        }

        void AddOutRequestDangleVerts(std::vector<GLfloat> &verts, const OutQosRequest &req, size_t inLayer, size_t outLayer, size_t totalOutLayers)
        {
            // local model coords
            float xStart = req.StartTimeMs * (1 / MsPerXUnit);
            float xEnd = (req.StartTimeMs + req.DurationMs) * (1 / MsPerXUnit);

            float yTop = 0.0f;
            float yBottom = 1.0f;

            float z = (float)(outLayer + 1) / (totalOutLayers + 1);

            // other properties
            float yOffset = -2.0f;
            float zOffset = inLayer * 3.0f;

            float r, g, b;
            if (req.CallerFault)
            {
                r = 0.7f; g = 0.0f; b = 1.0f;
            }
            else if (req.Success)
            {
                r = 0.3f; g = 0.3f; b = 1.0f;
            }
            else
            {
                r = 1.0f; g = 0.8f; b = 0.2f;
            }
            
            float aBot = 1.0f;
            float aTop = 0.0f;

            // just a plane
            verts.insert(verts.end(), { xStart, yBottom, z, r, g, b, aBot, yOffset, zOffset });
            verts.insert(verts.end(), { xEnd,   yBottom, z, r, g, b, aBot, yOffset, zOffset });
            verts.insert(verts.end(), { xEnd,   yTop,    z, r, g, b, aTop, yOffset, zOffset });

            verts.insert(verts.end(), { xEnd,   yTop,    z, r, g, b, aTop, yOffset, zOffset });
            verts.insert(verts.end(), { xStart, yBottom, z, r, g, b, aBot, yOffset, zOffset });
            verts.insert(verts.end(), { xStart, yTop,    z, r, g, b, aTop, yOffset, zOffset });
        }

        bool TestRayIntersection(const glm::vec3& pos, const glm::vec3& normalizedDir, const InQosRequest &req, size_t layer)
        {
            // world model coords
            float xStart = req.StartTimeMs * (1 / MsPerXUnit);
            float xEnd = (req.StartTimeMs + req.DurationMs) * (1 / MsPerXUnit);

            float yTop = -1.0f;
            float yBottom = 0.0f;

            float zNear = layer * 3.0f;
            float zFar = layer * 3.0f + 1.0f;

            // standard AABB
            glm::vec3 boxMin{xStart, yTop, zNear};
            glm::vec3 boxMax{xEnd, yBottom, zFar};

            glm::vec3 vecNear = (boxMin - pos) / normalizedDir;
            glm::vec3 vecFar = (boxMax - pos) / normalizedDir;

            glm::vec3 vecMin = glm::min(vecNear, vecFar);
            glm::vec3 vecMax = glm::max(vecNear, vecFar);

            float tNear = glm::max(glm::max(vecMin.x, vecMin.y), vecMin.z);
            float tFar = glm::min(glm::min(vecMax.x, vecMax.y), vecMax.z);

            return tNear < tFar;
        }
    };

    struct VisualizerWindow
    {
        std::string SetupFailure; // empty on success;

        HWND hwndQosWindow = 0;
        HWND hwndRenderWindow = 0;
        bool windowsFirstDrawHack = true;

        HWND hwndButResetStart = 0;
        HWND hwndButPlayLeftFast = 0;
        HWND hwndButPlayLeftSlow = 0;
        HWND hwndButPause = 0;
        HWND hwndButPlayRightSlow = 0;
        HWND hwndButPlayRightFast = 0;
        HWND hwndButResetEnd = 0;

        HWND hwndTimeScale = 0;

        HDC windowDc = 0;
        HGLRC windowGlContext = 0;

        std::unique_ptr<DrawerHelper> Drawer;
        QosRenderData RenderData;

        void SetupOpenGL()
        {
            windowDc = GetDC(hwndRenderWindow);

            PIXELFORMATDESCRIPTOR pfd =
            {
                .nSize = sizeof(PIXELFORMATDESCRIPTOR),
                .nVersion = 1,
                .dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
                .iPixelType = PFD_TYPE_RGBA,
                .cColorBits = 32,
                .cRedBits = 0, .cRedShift = 0, .cGreenBits = 0, .cGreenShift = 0, .cBlueBits = 0, .cBlueShift = 0, .cAlphaBits = 0, .cAlphaShift = 0,
                .cAccumBits = 0, .cAccumRedBits = 0, .cAccumGreenBits = 0, .cAccumBlueBits = 0, .cAccumAlphaBits = 0,
                .cDepthBits = 24,
                .cStencilBits = 0,
                .cAuxBuffers = 0,
                .iLayerType = PFD_MAIN_PLANE,
                .bReserved = 0,
                .dwLayerMask = 0,
                .dwVisibleMask = 0,
                .dwDamageMask = 0
            };

            int pixelFormatIndex = ChoosePixelFormat(windowDc, &pfd);
            SetPixelFormat(windowDc, pixelFormatIndex, &pfd);
            windowGlContext = wglCreateContext(windowDc);

            if (!windowGlContext)
            {
                SetupFailure = "Failed to create OpenGL context.";
                return;
            }

            if (!wglMakeCurrent(windowDc, windowGlContext))
            {
                SetupFailure = "Failed to set OpenGL context.";
                return;
            }

            auto getAnyGLFuncAddress = [](const char *name)
            {
                // work around windows bugs where wglGetProcAddress only returns a subset of valid opengl methods
                void* wglAddr = wglGetProcAddress(name);
                if (wglAddr && !(wglAddr == (void*)1 || wglAddr == (void*)2 || wglAddr == (void*)3 || wglAddr == (void*)-1)) // windows might return other non-valid non-zero values as well
                    return (glbinding::ProcAddress)wglAddr;

                HMODULE module = LoadLibraryA("opengl32.dll");
                if (!module)
                    return (glbinding::ProcAddress)nullptr;

                return (glbinding::ProcAddress)::GetProcAddress(module, name);
            };

            glbinding::initialize(getAnyGLFuncAddress, false);

            GlobalDebugOutput("OpenGL Version: " + glbinding::aux::ContextInfo::version().toString());
            GlobalDebugOutput("OpenGL Vender: " + glbinding::aux::ContextInfo::vendor());
            GlobalDebugOutput("OpenGL Renderer: " + glbinding::aux::ContextInfo::renderer());

            Drawer = std::make_unique<DrawerHelper>(hwndRenderWindow);
            SetupFailure = Drawer->SetupFailure;

            wglMakeCurrent(windowDc, NULL);
        }

        ~VisualizerWindow()
        {
            // free drawer with the context set
            wglMakeCurrent(windowDc, windowGlContext);
            glbinding::useCurrentContext();
            Drawer = nullptr;

            if (windowGlContext)
            {
                wglMakeCurrent(windowDc, NULL); 
                wglDeleteContext(windowGlContext);
                windowGlContext = 0;

                ReleaseDC(hwndRenderWindow, windowDc);
                windowDc = 0;
            }
        }
    };

    std::list<std::shared_ptr<VisualizerWindow>> AllInstances;

    LRESULT CALLBACK QosVisualizerWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        VisualizerWindow *vw = (VisualizerWindow*)GetWindowLongPtr(hwnd, GWLP_USERDATA);

        switch (uMsg)
        {
        case WM_CREATE:
        {
            CREATESTRUCT *cs = (CREATESTRUCT*)lParam;
            vw = (VisualizerWindow*)cs->lpCreateParams;
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)vw);

            RECT clientRect;
            GetClientRect(hwnd, &clientRect);

            vw->hwndRenderWindow = CreateWindow("LogCheetahQosRender", "", WS_CHILD | WS_VISIBLE, 0, 30, clientRect.right, clientRect.bottom - clientRect.top - 30, hwnd, (HMENU)0, hInstance, vw);

            CreateWindow(WC_STATIC, "Time Location:", WS_VISIBLE | WS_CHILD, 10, 7, 85, 21, hwnd, 0, hInstance, 0);
            vw->hwndButResetStart =    CreateWindow(WC_BUTTON, "|<", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 100, 3, 25, 25, hwnd, 0, hInstance, 0);
            vw->hwndButPlayLeftFast =  CreateWindow(WC_BUTTON, "<<", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 130, 3, 25, 25, hwnd, 0, hInstance, 0);
            vw->hwndButPlayLeftSlow =  CreateWindow(WC_BUTTON, "<",  WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 160, 3, 25, 25, hwnd, 0, hInstance, 0);
            vw->hwndButPause =         CreateWindow(WC_BUTTON, "||", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 190, 3, 25, 25, hwnd, 0, hInstance, 0);
            vw->hwndButPlayRightSlow = CreateWindow(WC_BUTTON, ">",  WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 220, 3, 25, 25, hwnd, 0, hInstance, 0);
            vw->hwndButPlayRightFast = CreateWindow(WC_BUTTON, ">>", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 250, 3, 25, 25, hwnd, 0, hInstance, 0);
            vw->hwndButResetEnd =      CreateWindow(WC_BUTTON, ">|", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 280, 3, 25, 25, hwnd, 0, hInstance, 0);

            CreateWindow(WC_STATIC, "Time Scale:", WS_VISIBLE | WS_CHILD, 400, 7, 65, 21, hwnd, 0, hInstance, 0);
            vw->hwndTimeScale = CreateWindowEx(0, TRACKBAR_CLASS, "TimeScale", WS_CHILD | WS_VISIBLE, 465, 3, 200, 25, hwnd, 0, hInstance, 0);
            SendMessage(vw->hwndTimeScale, TBM_SETRANGE, (WPARAM)true, (LPARAM)MAKELONG(10, 1000));
            SendMessage(vw->hwndTimeScale, TBM_SETPAGESIZE, 0, (LPARAM)10);
            SendMessage(vw->hwndTimeScale, TBM_SETPOS, (WPARAM)TRUE, (LPARAM)100); // default to 100 ms per x unit in space

            //
            FixChildFonts(hwnd);

            return 0;
        }
        case WM_COMMAND:
        {
            if ((HWND)lParam == vw->hwndButResetStart)
                vw->Drawer->SetScrollPositionMs(0);
            else if ((HWND)lParam == vw->hwndButPlayLeftFast)
                vw->Drawer->ScrollMultiplier = -10.0;
            else if ((HWND)lParam == vw->hwndButPlayLeftSlow)
                vw->Drawer->ScrollMultiplier = -1.0;
            else if ((HWND)lParam == vw->hwndButPause)
                vw->Drawer->ScrollMultiplier = 0.0;
            else if ((HWND)lParam == vw->hwndButPlayRightSlow)
                vw->Drawer->ScrollMultiplier = 1.0;
            else if ((HWND)lParam == vw->hwndButPlayRightFast)
                vw->Drawer->ScrollMultiplier = 10.0;
            else if ((HWND)lParam == vw->hwndButResetEnd)
                vw->Drawer->SetScrollPositionMs(vw->RenderData.MaxDataTimeMs);
        }
        break;
        case WM_HSCROLL:
        {
            if ((HWND)lParam == vw->hwndTimeScale)
            {
                LRESULT newTimeScale = SendMessage(vw->hwndTimeScale, TBM_GETPOS, 0, 0);
                if (newTimeScale != 0)
                    vw->Drawer->MsPerXUnit = (float)newTimeScale;
            }
        }
        break;
        case WM_SIZE:
        {
            RECT clientRect;
            GetClientRect(hwnd, &clientRect);
            SetWindowPos(vw->hwndRenderWindow, 0, 0, 30, clientRect.right, clientRect.bottom - clientRect.top - 30, 0);
        }
        break;
        case WM_DESTROY: case WM_QUIT: case WM_CLOSE:
            AllInstances.remove_if([&](std::shared_ptr<VisualizerWindow> p) {return p.get() == vw; });
            break;
        }

        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }

    LRESULT CALLBACK QosRenderWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        VisualizerWindow *vw = (VisualizerWindow*)GetWindowLongPtr(hwnd, GWLP_USERDATA);

        switch (uMsg)
        {
        case WM_CREATE:
        {
            CREATESTRUCT *cs = (CREATESTRUCT*)lParam;
            vw = (VisualizerWindow*)cs->lpCreateParams;
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)vw);

            return 0;
        }
        case WM_LBUTTONDOWN:
        {
            int mousePosX = GET_X_LPARAM(lParam); 
            int mousePosY = GET_Y_LPARAM(lParam); 

            RECT clientRect;
            GetClientRect(hwnd, &clientRect);
            if (clientRect.right - clientRect.left <= 0 || clientRect.bottom - clientRect.top <= 0)
                break;

            // open a new filtered view for the request they clicked
            float screenHomogeneousX = ((float)(mousePosX - clientRect.left) / (float)(clientRect.right - clientRect.left)) * 2 - 1;
            float screenHomogeneousY = 1 - (float)(mousePosY - clientRect.top) / (float)(clientRect.bottom - clientRect.top) * 2;

            glm::vec3 rayOrigin = vw->Drawer->ScreenToWorldSpace(glm::vec3(screenHomogeneousX, screenHomogeneousY, 0));
            glm::vec3 rayDir = vw->Drawer->ScreenToWorldSpace(glm::vec3(screenHomogeneousX, screenHomogeneousY, 1)) - rayOrigin;

            const InQosRequest *clickedRequest = vw->Drawer->IntersectWorldRayWithQosData(rayOrigin, rayDir, vw->RenderData);
            if (clickedRequest)
            {
                size_t cvColumnIndex = std::numeric_limits<size_t>::max();
                for (size_t i = 0; i < globalLogs.Columns.size(); ++i)
                {
                    if (globalLogs.Columns[i].UniqueName == "cV")
                    {
                        cvColumnIndex = i;
                        break;
                    }
                }

                if (cvColumnIndex != std::numeric_limits<size_t>::max())
                {
                    std::vector<LogFilterEntry> newFilters;
                    newFilters.emplace_back();
                    newFilters.back().Column = (int)cvColumnIndex;
                    newFilters.back().MatchCase = true;
                    newFilters.back().MatchSubstring = true;
                    newFilters.back().Value = clickedRequest->CorrelationBase;

                    OpenNewFilteredMainLogView(newFilters);
                }
            }
        }
        break;
        case WM_MOUSEMOVE:
        {
            int mousePosX = GET_X_LPARAM(lParam); 
            int mousePosY = GET_Y_LPARAM(lParam); 

            RECT clientRect;
            GetClientRect(hwnd, &clientRect);
            if (clientRect.right - clientRect.left <= 0 || clientRect.bottom - clientRect.top <= 0)
                break;

            // highlight a request under the mouse
            vw->Drawer->PickerHightlightScreenX = ((float)(mousePosX - clientRect.left) / (float)(clientRect.right - clientRect.left)) * 2 - 1;
            vw->Drawer->PickerHightlightScreenY = 1 - (float)(mousePosY - clientRect.top) / (float)(clientRect.bottom - clientRect.top) * 2;

            // fix WM_MOUSELEAVE never firing
            TRACKMOUSEEVENT tme{0};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
        }
        break;
        case WM_MOUSELEAVE:
        {
            vw->Drawer->PickerHightlightScreenX = vw->Drawer->PickerHightlightScreenY = 0.0f;
        }
        break;
        case WM_PAINT:
        {
            if (!vw->windowGlContext)
                break;

            wglMakeCurrent(vw->windowDc, vw->windowGlContext);
            glbinding::useCurrentContext();

            RECT clientRect;
            GetClientRect(hwnd, &clientRect);
            if (clientRect.right - clientRect.left <= 0 || clientRect.bottom - clientRect.top <= 0)
                break;
            
            glViewport(clientRect.left, clientRect.top, clientRect.right - clientRect.left, clientRect.bottom - clientRect.top);
            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            glClear(ClearBufferMask::GL_DEPTH_BUFFER_BIT | ClearBufferMask::GL_COLOR_BUFFER_BIT);

            float aspect = ((float)clientRect.right - clientRect.left) / ((float)clientRect.bottom - clientRect.top);
            if (vw->Drawer)
                vw->Drawer->Draw(aspect, vw->RenderData);

            wglSwapLayerBuffers(vw->windowDc, WGL_SWAP_MAIN_PLANE);
            wglMakeCurrent(vw->windowDc, NULL);

            if (vw->Drawer->ScrollMultiplier != 0.0)
                InvalidateRect(hwnd, nullptr, true); // force immediate redraw

            // hack around a bug where windows doesn't show the parent's child buttons until the window is first moved
            if (vw->windowsFirstDrawHack)
            {
                vw->windowsFirstDrawHack = false;
                UpdateWindow(vw->hwndQosWindow);
            }
        }
        return 0;
        case WM_ERASEBKGND:
        return 0;
        }

        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }

    bool didRegistorWindowClass = false;
    bool RegisterWindowClass()
    {
        if (didRegistorWindowClass)
            return true;

        WNDCLASS wc;
        wc.style = 0;
        wc.lpfnWndProc = (WNDPROC)QosVisualizerWindowProc;
        wc.cbClsExtra = 0;
        wc.cbWndExtra = 0;
        wc.hInstance = hInstance;
        wc.hIcon = (HICON)LoadImage(hInstance, MAKEINTRESOURCE(IDI_ICON2), IMAGE_ICON, 0, 0, LR_CREATEDIBSECTION);
        wc.hCursor = LoadCursor((HINSTANCE)0, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(1 + COLOR_3DFACE);
        wc.lpszMenuName = nullptr;
        wc.lpszClassName = "LogCheetahQosVisualizer";

        if (!RegisterClass(&wc))
            return false;

        wc.style = CS_OWNDC; // Needed for OpenGL
        wc.lpfnWndProc = (WNDPROC)QosRenderWindowProc;
        wc.cbClsExtra = 0;
        wc.cbWndExtra = 0;
        wc.hInstance = hInstance;
        wc.hIcon = (HICON)LoadImage(hInstance, MAKEINTRESOURCE(IDI_ICON2), IMAGE_ICON, 0, 0, LR_CREATEDIBSECTION);
        wc.hCursor = LoadCursor((HINSTANCE)0, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(1 + COLOR_3DFACE);
        wc.lpszMenuName = nullptr;
        wc.lpszClassName = "LogCheetahQosRender";

        if (!RegisterClass(&wc))
            return false;

        didRegistorWindowClass = true;
        return true;
    }

    std::optional<uint16_t> FindOptionalColumnIndex(const std::string &name)
    {
        for (size_t i = 0; i < globalLogs.Columns.size(); ++i)
        {
            if (globalLogs.Columns[i].UniqueName == name)
            {
                return (uint16_t)i;
            }
        }

        return {};
    }

    std::optional<std::vector<uint16_t>> FindRequiredColumnIndices(const std::vector<std::string> &names)
    {
        std::vector<uint16_t> indices;

        for (const std::string &name : names)
        {
            std::optional<uint16_t> index = FindOptionalColumnIndex(name);
            if (index.has_value())
                indices.emplace_back((uint16_t)index.value());
            else
                return {};
        }

        return indices;
    }

    // includes the trailing dot
    std::string TrimLastCvNumber(const std::string &fullCv)
    {
        std::string trimmedCv = fullCv;
        size_t lastDotPos = fullCv.find_last_of('.');
        if (lastDotPos != std::string::npos)
            trimmedCv.resize(lastDotPos + 1);
        return trimmedCv;
    }

    // no trailing dot
    std::string TrimCvToBase(const std::string &fullCv)
    {
        std::string trimmedCv = fullCv;
        size_t lastDotPos = fullCv.find_first_of('.');
        if (lastDotPos != std::string::npos)
            trimmedCv.resize(lastDotPos);
        return trimmedCv;
    }

    uint64_t TimeStampToMs(const std::string &timestamp)
    {
        //Sample timestamp: 2016-07-27T22:56:47.0107862Z

        std::istringstream stream(timestamp);
        std::chrono::sys_time<std::chrono::milliseconds> tp;
        std::chrono::from_stream(stream, "%Y-%m-%dT%H:%M:%S%Z", tp);
        return std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count();
    }

    std::shared_ptr<VisualizerWindow> ParseDataAndStuff(const std::vector<uint32_t> &rowsToUse)
    {
        AllInstances.emplace_back(std::make_shared<VisualizerWindow>());
        std::shared_ptr<VisualizerWindow> vw = AllInstances.back();

        GuiStatusManager::ShowBusyDialogAndRunManager([&](GuiStatusManager &monitorManager)
        {
            // Setup monitor dialog
            GuiStatusManager::AutoSection monitorParseSection { monitorManager, false, "Parse QoS Logs", 1 };
            GuiStatusMonitor &monitorParse = monitorParseSection.Section().PartIndex(0);
            monitorParse.SetProgressFeatures(1); //don't show active work until it starts

            GuiStatusManager::AutoSection monitorTimestampsSection { monitorManager, false, "Compute Timestamps", 1 };
            GuiStatusMonitor &monitorTimestamps = monitorTimestampsSection.Section().PartIndex(0);
            monitorTimestamps.SetProgressFeatures(1); //don't show active work until it starts

            GuiStatusManager::AutoSection monitorSortSection { monitorManager, false, "Sorting Data", 1 };
            GuiStatusMonitor &monitorSort = monitorSortSection.Section().PartIndex(0);
            monitorSort.SetProgressFeatures(1); //don't show active work until it starts

            GuiStatusManager::AutoSection monitorMatchSection { monitorManager, false, "Matching Incoming/Outgoing Requests", 1 };
            GuiStatusMonitor &monitorMatch = monitorMatchSection.Section().PartIndex(0);
            monitorMatch.SetProgressFeatures(1); //don't show active work until it starts

            GuiStatusManager::AutoSection monitorLayersSection { monitorManager, false, "Building Render Layers", 1};
            GuiStatusMonitor &monitorLayers = monitorLayersSection.Section().PartIndex(0);
            monitorLayers.SetProgressFeatures(1); //don't show active work until it starts

            // Find all neccesary columns
            std::optional<std::vector<uint16_t>> allQosCols = FindRequiredColumnIndices({"data.baseType", "data.baseData.succeeded", "data.baseData.latencyMs", "time"});
            if (!allQosCols.has_value())
                return;

            uint16_t indexBaseType = allQosCols.value()[0];
            uint16_t indexSucceeded = allQosCols.value()[1];
            uint16_t indexLatencyMs = allQosCols.value()[2];
            uint16_t indexTimestamp = allQosCols.value()[3];

            // Find optional columns
            std::optional<uint16_t> indexRequestStatus = FindOptionalColumnIndex("data.baseData.requestStatus");
            std::optional<uint16_t> indexCv = FindOptionalColumnIndex("cV");

            // Grab qos data from all rows
            monitorParse.SetControlFeatures(true);
            monitorParse.SetProgressFeatures(rowsToUse.size(), "kiloline", 1000);

            std::vector<InQosRequest> allInRequests;
            std::vector<OutQosRequest> allOutRequests;

            for (uint32_t row : rowsToUse)
            {
                if (monitorManager.IsCancelling())
                    break;

                LogEntry &line = globalLogs.Lines[row];

                // Determine whether this row is a qos entry with data we care about
                ExternalSubstring<const char> strBaseType = line.GetColumnNumberValue(indexBaseType);
                ExternalSubstring<const char> strSucceeded = line.GetColumnNumberValue(indexSucceeded);
                ExternalSubstring<const char> strLatencyMs = line.GetColumnNumberValue(indexLatencyMs);
                ExternalSubstring<const char> strTimestamp = line.GetColumnNumberValue(indexTimestamp);

                ExternalSubstring<const char> strRequestStatus;
                if (indexRequestStatus.has_value())
                    strRequestStatus = line.GetColumnNumberValue(indexRequestStatus.value());

                ExternalSubstring<const char> strCv;
                if (indexCv.has_value())
                    strCv = line.GetColumnNumberValue(indexCv.value());

                if (!strSucceeded.empty() && !strLatencyMs.empty() && !strTimestamp.empty())
                {
                    auto parseQosBase = [&](QosData &target)
                    {
                        target.Success = strSucceeded.CaseInsensitiveCompare("true"s);
                        char* unused = nullptr;
                        long latencyMs = strtol(strLatencyMs.str().c_str(), &unused, 10);
                        if (latencyMs < 0) // some libraries log bogus data
                            latencyMs = 0;

                        target.DurationMs = latencyMs;
                        target.DurationMs = std::max(target.DurationMs, (uint64_t)1); // min of 1 ms duration so it's always visible
                        target.DurationMs = std::min(target.DurationMs, (uint64_t)((std::chrono::milliseconds)60min).count()); // max of 1 hour to deal with potentially bogus data better
                        target.EndTimestamp = strTimestamp.str();

                        if (strRequestStatus == "4"s)
                            target.CallerFault = true;
                    
                        target.CorrelationFull = strCv.str();
                        target.CorrelationBase = TrimCvToBase(target.CorrelationFull);
                    };

                    if (strBaseType == "Ms.Qos.IncomingServiceRequest"s)
                    {
                        InQosRequest &req = allInRequests.emplace_back();
                        parseQosBase(req);
                    }
                    else if (strBaseType == "Ms.Qos.OutgoingServiceRequest"s)
                    {
                        OutQosRequest &req = allOutRequests.emplace_back();
                        parseQosBase(req);
                    }
                }

                monitorParse.AddProgress(1);
            }

            if (!monitorManager.IsCancelling() && !allInRequests.empty())
            {
                // Walk all the data and compute start timestamps relative to some arbitrary epoch
                monitorTimestamps.SetProgressFeatures(5);

                uint64_t epochMs = std::numeric_limits<uint64_t>::max();

                for (InQosRequest &req : allInRequests)
                {
                    req.StartTimeMs = TimeStampToMs(req.EndTimestamp);
                    if (req.StartTimeMs != 0)
                    {
                        req.StartTimeMs -= req.DurationMs;

                        if (req.StartTimeMs < epochMs)
                            epochMs = req.StartTimeMs;
                    }
                }

                monitorTimestamps.AddProgress(1);

                for (OutQosRequest &req : allOutRequests)
                {
                    req.StartTimeMs = TimeStampToMs(req.EndTimestamp);
                    if (req.StartTimeMs != 0)
                    {
                        req.StartTimeMs -= req.DurationMs;

                        // Note that we don't update epoch for outgoing qos requests, since our starting view window is based on incoming requests
                    }
                }

                monitorTimestamps.AddProgress(1);

                // Filter out data that had bad timestamps
                for (auto iter = allInRequests.begin(); iter != allInRequests.end(); )
                {
                    if (iter->StartTimeMs == 0)
                        iter = allInRequests.erase(iter);
                    else
                        ++iter;
                }

                for (auto iter = allOutRequests.begin(); iter != allOutRequests.end(); )
                {
                    if (iter->StartTimeMs == 0)
                        iter = allOutRequests.erase(iter);
                    else
                        ++iter;
                }

                monitorTimestamps.AddProgress(1);

                // Adjust all timestamps to use a 0-based epoch
                for (InQosRequest &req : allInRequests)
                {
                    req.StartTimeMs -= epochMs;

                    if (req.StartTimeMs + req.DurationMs > vw->RenderData.MaxDataTimeMs)
                        vw->RenderData.MaxDataTimeMs = req.StartTimeMs + req.DurationMs;
                }

                monitorTimestamps.AddProgress(1);

                for (OutQosRequest &req : allOutRequests)
                {
                    req.StartTimeMs -= epochMs;

                    if (req.StartTimeMs + req.DurationMs > vw->RenderData.MaxDataTimeMs)
                        vw->RenderData.MaxDataTimeMs = req.StartTimeMs + req.DurationMs;
                }

                monitorTimestamps.AddProgress(1);

                // Sort requests by start time
                if (!monitorManager.IsCancelling())
                {
                    monitorSort.SetProgressFeatures(2);

                    std::ranges::sort(allInRequests, [](const InQosRequest &a, const InQosRequest &b) { return a.StartTimeMs < b.StartTimeMs; });
                    monitorSort.AddProgress(1);

                    std::ranges::sort(allOutRequests, [](const OutQosRequest &a, const OutQosRequest &b) { return a.StartTimeMs < b.StartTimeMs; });
                    monitorSort.AddProgress(1);
                }

                // Move outgoing requests to their corresponding incoming request
                monitorMatch.SetProgressFeatures(allOutRequests.size() * 2, "kiloline", 1000);

                std::unordered_map<std::string, std::vector<InQosRequest*>> inCvBaseMap;
                for (InQosRequest &inReq : allInRequests)
                {
                    auto iterFound = inCvBaseMap.find(inReq.CorrelationBase);
                    if (iterFound == inCvBaseMap.end())
                    {
                        inCvBaseMap.emplace(inReq.CorrelationBase, std::vector<InQosRequest*>{ &inReq });
                    }
                    else
                    {
                        iterFound->second.emplace_back(&inReq);
                    }            

                    monitorMatch.AddProgress(1);
                }

                for (OutQosRequest &outReq : allOutRequests)
                {
                    if (monitorManager.IsCancelling())
                        break;

                    auto iterFound = inCvBaseMap.find(outReq.CorrelationBase);
                    if (iterFound != inCvBaseMap.end())
                    {
                        // see if any match on exact CV rather than just the base
                        for (InQosRequest *inReq : iterFound->second)
                        {
                            std::string inTrimmedCv = TrimLastCvNumber(inReq->CorrelationFull);
                            if (outReq.CorrelationFull.starts_with(inTrimmedCv))
                            {
                                inReq->AssociatedOutRequests.emplace_back(std::move(outReq));
                                break;
                            }
                        }
                    }

                    monitorMatch.AddProgress(1);
                }

                // Assign render layers so that no request overlaps in time
                monitorLayers.SetProgressFeatures(allInRequests.size(), "kiloline", 1000);

                std::vector<uint64_t> layerActiveUntil;
                for (InQosRequest &req : allInRequests)
                {
                    if (monitorManager.IsCancelling())
                        break;

                    uint64_t minStartTime = req.StartTimeMs;
                    uint64_t chosenLayer = std::numeric_limits<uint64_t>::max();

                    for (size_t i = 0; i < layerActiveUntil.size(); ++i)
                    {
                        if (minStartTime > layerActiveUntil[i])
                        {
                            chosenLayer = i;
                            break;
                        }
                    }

                    if (chosenLayer == std::numeric_limits<uint64_t>::max()) // need a new layer
                    {
                        chosenLayer = layerActiveUntil.size();
                        layerActiveUntil.emplace_back();
                    }

                    if (vw->RenderData.Layers.size() < chosenLayer + 1)
                        vw->RenderData.Layers.resize(chosenLayer + 1);

                    layerActiveUntil[chosenLayer] = req.StartTimeMs + req.DurationMs + MsBufferBetweenRequestsOnLayer;

                    // move the request to the final render data
                    vw->RenderData.Layers[chosenLayer].Requests.emplace_back(std::move(req));

                    monitorLayers.AddProgress(1);
                }
            }

            if (monitorManager.IsCancelling())
                vw = nullptr;
        });

        return vw;
    }
}

void ShowQosVisualizer(const std::vector<uint32_t> &rowsToUse)
{
    if (!RegisterWindowClass())
        return;

    std::shared_ptr<VisualizerWindow> vw = ParseDataAndStuff(rowsToUse);

    if (vw)
    {
        //NOTE: window creation has to happen from the main thread
        std::stringstream windowText;
        windowText << "Qos Visualizer";
        vw->hwndQosWindow = CreateWindow("LogCheetahQosVisualizer", windowText.str().c_str(), WS_OVERLAPPED | WS_OVERLAPPEDWINDOW | WS_SIZEBOX | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 1600, 800, 0, (HMENU)0, hInstance, vw.get());

        if (!vw->hwndQosWindow || !vw->hwndRenderWindow)
            return;

        vw->SetupOpenGL();

        if (!vw->SetupFailure.empty())
        {
            std::string err = vw->SetupFailure;
            DestroyWindow(vw->hwndQosWindow);
            MessageBox(hwndMain, err.c_str(), "Windows Error - Check Debug Pane", MB_OK);
        }
        else
        {
            size_t inRequests = 0;
            size_t outRequests = 0;

            for (const QosRenderLayer &layer : vw->RenderData.Layers)
            {
                for (const InQosRequest &inReq : layer.Requests)
                {
                    ++inRequests;
                    outRequests += inReq.AssociatedOutRequests.size();
                }
            }

            std::string newWindowText = "QoS Visualizer - " + std::to_string(inRequests) + " incoming and " + std::to_string(outRequests) + " outgoing requests.";
            SetWindowText(vw->hwndQosWindow, newWindowText.c_str());
        }
    }
}
