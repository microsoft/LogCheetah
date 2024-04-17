#include <Windows.h>
#include <Windowsx.h>
#include <CommCtrl.h>
#include <string>
#include <chrono>
using namespace std::chrono_literals;

//why does this header have warnings?
#pragma warning(disable:4458)
#include <gdiplus.h>

#include "CatWindow.h"
#include "Preferences.h"

namespace
{
    HWND hwndCat = 0;
    HWND hwndCatFollow = 0;
    bool mouseDragActive = false;
    int minScreenY = 0;
    int maxScreenY = 1;
    bool windowMaximized = false;
    bool alreadyLoadedResources = false;

    std::vector<Gdiplus::Image*> idleImagesR;
    std::vector<Gdiplus::Image*> idleImagesL;
    std::vector<Gdiplus::Image*> fallingImagesR;
    std::vector<Gdiplus::Image*> fallingImagesL;
    std::vector<Gdiplus::Image*> climbingImagesR;
    std::vector<Gdiplus::Image*> climbingImagesL;
    std::vector<Gdiplus::Image*> walkingImagesR;
    std::vector<Gdiplus::Image*> walkingImagesL;
    std::vector<Gdiplus::Image*> grabbedImagesR;
    std::vector<Gdiplus::Image*> grabbedImagesL;

    std::tuple<std::vector<Gdiplus::Image*>, std::vector<Gdiplus::Image*>> LoadImageSets(const std::wstring nameBase)
    {
        std::vector<Gdiplus::Image*> imagesR;
        std::vector<Gdiplus::Image*> imagesL;
        for (int i = 1; i <= 32; ++i)
        {
            std::wstring name = L"cats\\" + nameBase + std::to_wstring(i) + L".png";
            Gdiplus::Image* newImageR = Gdiplus::Image::FromFile(name.c_str());
            if (!newImageR || newImageR->GetLastStatus() != Gdiplus::Ok)
                break;;

            imagesR.emplace_back(newImageR);

            imagesL.emplace_back(newImageR->Clone());
            imagesL.back()->RotateFlip(Gdiplus::RotateNoneFlipX);
        }

        return { imagesR, imagesL };
    }

    enum class CatState
    {
        Idle,
        Falling,
        Climbing,
        Walking,
        Grabbed
    };

    class CatController
    {
    public:
        float X = 0;
        float Y = 0;

        void SetGrabbed(bool isGrabbed)
        {
            if (state == CatState::Grabbed && !isGrabbed)
                state = CatState::Falling;
            else if (state != CatState::Grabbed && isGrabbed)
                state = CatState::Grabbed;
        }

        void Update()
        {
            //initialize desires if we're new
            if (minDesiredX == 0 && maxDesiredX == 0 && desiredY == 0)
            {
                minDesiredX = X - 1;
                maxDesiredX = X + 1;
                desiredY = Y;
            }

            //update
            std::chrono::high_resolution_clock::time_point now = std::chrono::high_resolution_clock::now();
            float secondsPassed = std::chrono::duration_cast<std::chrono::microseconds>(now - lastTime).count() / 1000000.0f;
            if (secondsPassed > 0.2f)
                secondsPassed = 0.2f;

            UpdateState(secondsPassed);
            UpdateMovement(secondsPassed);
            UpdateFrame(secondsPassed);

            if (X > lastX)
                isFacingRight = true;
            else if (X < lastX)
                isFacingRight = false;

            lastTime = now;
            lastX = X;
        }

        Gdiplus::Image* GetFrame()
        {
            std::vector<Gdiplus::Image*> *frameSet = nullptr;

            if (state == CatState::Idle)
                frameSet = isFacingRight ? &idleImagesR : &idleImagesL;
            else if (state == CatState::Falling)
                frameSet = isFacingRight ? &fallingImagesR : &fallingImagesL;
            else if (state == CatState::Climbing)
                frameSet = isFacingRight ? &climbingImagesR : &climbingImagesL;
            else if (state == CatState::Walking)
                frameSet = isFacingRight ? &walkingImagesR : &walkingImagesL;
            else if (state == CatState::Grabbed)
                frameSet = isFacingRight ? &grabbedImagesR : &grabbedImagesL;

            if (!frameSet || frameSet->empty())
                frameSet = isFacingRight ? &idleImagesR : &idleImagesL;

            if (!frameSet || frameSet->empty())
                return nullptr;

            return (*frameSet)[frame%frameSet->size()];
        }

        int GetFrameYOffset()
        {
            if (state == CatState::Grabbed)
                return -26;
            else if (state == CatState::Climbing)
                return -20;

            return -48;
        }

    private:
        void UpdateState(float secondsPassed)
        {
            //if we've been grabbed, nothing else matters
            if (state == CatState::Grabbed)
                return;

            //figure out where we want to be
            RECT followTargetRect = { 0 };
            GetWindowRect(hwndCatFollow, &followTargetRect);
            minDesiredX = (float)(followTargetRect.left + (followTargetRect.right - followTargetRect.left) / 3);
            maxDesiredX = (float)(minDesiredX + (followTargetRect.right - followTargetRect.left) / 3);
            if (maxDesiredX - minDesiredX < 10)
                maxDesiredX = minDesiredX + 10;
            desiredY = (float)followTargetRect.top;

            if (windowMaximized || desiredY < minScreenY + 25) //maximized or top would make us clip - go sit next to the setup button instead
            {
                minDesiredX = followTargetRect.right - 110.0f;
                maxDesiredX = minDesiredX + 10;
                desiredY = followTargetRect.top + 55.0f;
            }

            //are we done climbing or done walking?
            if (state == CatState::Climbing)
            {
                if (fabs(Y - desiredY) <= 4.0f)
                    state = CatState::Idle;
            }

            if (state == CatState::Walking)
            {
                if (X >= minDesiredX && X <= maxDesiredX)
                    state = CatState::Idle;
            }

            //do we need to start falling?
            if (state == CatState::Idle || state == CatState::Walking)
            {
                if (fabs(Y - desiredY) > 4.0f || X<(float)followTargetRect.left || X>(float)followTargetRect.right)
                    state = CatState::Falling;
            }

            if (state == CatState::Climbing)
            {
                if (X<(float)followTargetRect.left || X>(float)followTargetRect.right || Y<(float)followTargetRect.top || Y>(float)followTargetRect.bottom)
                    state = CatState::Falling;
            }

            //do we need to start climbing?
            if (state == CatState::Falling)
            {
                if (X > (float)followTargetRect.left && X<(float)followTargetRect.right && Y>(float)followTargetRect.top && Y < (float)followTargetRect.bottom)
                    state = CatState::Climbing;
            }

            //do we need to start walking?
            if (state == CatState::Idle)
            {
                if (fabs(Y - desiredY) <= 4.0f && (X<minDesiredX || X>maxDesiredX))
                    state = CatState::Walking;
            }
        }

        void UpdateMovement(float secondsPassed)
        {
            //if we've been grabbed, we don't move on our own
            if (state == CatState::Grabbed)
                return;

            //state-based movement
            RECT followTargetRect = { 0 };
            GetWindowRect(hwndCatFollow, &followTargetRect);

            if (state == CatState::Falling)
            {
                Y += 300 * secondsPassed;

                if (Y > maxScreenY + 64) //we fell off the bottom
                {
                    X = (float)(followTargetRect.left + (followTargetRect.right - followTargetRect.left) / 2);
                    Y = minScreenY - 64.0f;
                }
            }
            else if (state == CatState::Climbing)
            {
                float yChange = -250 * secondsPassed;
                if (Y + yChange < desiredY) //we reached the top
                    Y = desiredY;
                else
                    Y += yChange;
            }
            else if (state == CatState::Walking)
            {
                if (X < minDesiredX)
                    X += 200 * secondsPassed;
                else if (X > maxDesiredX)
                    X += -200 * secondsPassed;
            }
        }

        void UpdateFrame(float secondsPassed)
        {
            float secondsPerFrame = 0.1f;
            if (state == CatState::Idle)
                secondsPerFrame = 0.25f;
            else if (state == CatState::Climbing)
                secondsPerFrame = 0.085f;
            else if (state == CatState::Grabbed)
                secondsPerFrame = 0.3f;

            secondsUntilNextFrame -= secondsPassed;
            if (secondsUntilNextFrame < 0)
            {
                secondsUntilNextFrame += secondsPerFrame;
                ++frame;
            }
        }

        CatState state = CatState::Idle;
        bool isFacingRight = true;

        float minDesiredX = 0;
        float maxDesiredX = 0;
        float desiredY = 0;
        float lastX = 0.0f;

        int frame = 0;
        std::chrono::high_resolution_clock::time_point lastTime = std::chrono::high_resolution_clock::now();
        float secondsUntilNextFrame = 0;
    };

    CatController cat;

    void UpdateAndDrawCat(HDC dc)
    {
        if (!hwndCat || !hwndCatFollow)
            return;

        minScreenY = GetSystemMetrics(SM_YVIRTUALSCREEN);
        maxScreenY = GetSystemMetrics(SM_CYVIRTUALSCREEN) + minScreenY;
        if (maxScreenY - minScreenY == 0)
            ++maxScreenY;

        WINDOWPLACEMENT windowPlacement = { 0 };
        if (GetWindowPlacement(hwndCatFollow, &windowPlacement))
            windowMaximized = windowPlacement.showCmd == SW_SHOWMAXIMIZED;

        cat.SetGrabbed(mouseDragActive);
        if (mouseDragActive)
        {
            POINT mouse = { 0 };
            if (GetCursorPos(&mouse))
            {
                cat.X = (float)mouse.x;
                cat.Y = (float)mouse.y;
            }
        }

        cat.Update();

        SetWindowPos(hwndCat, 0, (int)cat.X - 32, (int)cat.Y + cat.GetFrameYOffset(), 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

        Gdiplus::Graphics gdip(dc);
        Gdiplus::Image *frame = cat.GetFrame();
        if (frame)
            gdip.DrawImage(frame, 0, 0, 64, 64);
    }

    void CALLBACK CatTimerProc(HWND, UINT, UINT_PTR, DWORD)
    {
        if (!hwndCat)
            return;

        RedrawWindow(hwndCat, nullptr, 0, RDW_UPDATENOW | RDW_INVALIDATE);
    }

    LRESULT CALLBACK CatWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        switch (uMsg)
        {
        case WM_CREATE:
        {
            SetTimer(hwnd, 1, 1000 / 60, CatTimerProc);
            return 0;
        }
        case WM_DESTROY:
        {
            return 0;
        }
        case WM_PAINT:
        {
            PAINTSTRUCT paint = { 0 };
            HDC dc = BeginPaint(hwnd, &paint);
            UpdateAndDrawCat(dc);
            EndPaint(hwnd, &paint);

            //return 0; //for unknown reasons, this makes the timer not ever work.. so we'll break instead.
            break;
        }
        case WM_LBUTTONDOWN:
        {
            mouseDragActive = true;
            CatTimerProc(hwnd, 0, 0, 0);
            return 0;
        }
        case WM_LBUTTONUP:
        {
            mouseDragActive = false;
            CatTimerProc(hwnd, 0, 0, 0);
            return 0;
        }
        }

        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
}

void InitCatWindow(HWND followTarget)
{
    ShutdownCatWindow();

    // see if we're allowed to exist and pick our theme
    if (!Preferences::AllowCats)
        return;

    std::wstring catTheme = Preferences::ForceCats ? L"plain" : L"";

    SYSTEMTIME startupTime = { 0 };
    GetLocalTime(&startupTime);
    if (startupTime.wMonth == 4 && startupTime.wDay == 1)
        catTheme = L"plain";
    else if (startupTime.wMonth == 10 && startupTime.wDay >= 27 && startupTime.wDay <= 31)
        catTheme = L"halloween";
    else if (startupTime.wMonth == 12 && startupTime.wDay >= 21 && startupTime.wDay <= 27)
        catTheme = L"xmas";

    if (catTheme.empty())
        return;

    if (!alreadyLoadedResources)
    {
        // init gdi+ and load our images
        static std::once_flag initGdiPlus;
        std::call_once(initGdiPlus, [&]()
        {
            Gdiplus::GdiplusStartupInput gdiPlusStartup;
            ULONG_PTR gdiPlusToken;
            Gdiplus::GdiplusStartup(&gdiPlusToken, &gdiPlusStartup, nullptr);

            std::tie(idleImagesR, idleImagesL) = LoadImageSets(catTheme + L"\\idle");
            std::tie(fallingImagesR, fallingImagesL) = LoadImageSets(catTheme + L"\\falling");
            std::tie(climbingImagesR, climbingImagesL) = LoadImageSets(catTheme + L"\\climbing");
            std::tie(walkingImagesR, walkingImagesL) = LoadImageSets(catTheme + L"\\walking");
            std::tie(grabbedImagesR, grabbedImagesL) = LoadImageSets(catTheme + L"\\grabbed");
        });

        // if we couldn't load images for some weird reason, just bail on the whole thing
        if (idleImagesR.empty())
            return;

        WNDCLASS wc;
        wc.style = 0;
        wc.lpfnWndProc = (WNDPROC)CatWindowProc;
        wc.cbClsExtra = 0;
        wc.cbWndExtra = 0;
        wc.hInstance = hInstance;
        wc.hIcon = 0;
        wc.hCursor = LoadCursor((HINSTANCE)0, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(1 + COLOR_3DFACE);
        wc.lpszMenuName = nullptr;
        wc.lpszClassName = "LogCheetahCatBox";

        if (!RegisterClass(&wc))
            return;

        alreadyLoadedResources = true;
    }

    hwndCatFollow = followTarget;

    hwndCat = CreateWindowEx(WS_EX_LAYERED | WS_EX_NOACTIVATE, "LogCheetahCatBox", "Meow", WS_VISIBLE | WS_POPUP, CW_USEDEFAULT, CW_USEDEFAULT, 64, 64, followTarget, (HMENU)0, hInstance, (LPVOID)0);
    SetLayeredWindowAttributes(hwndCat, RGB(255, 255, 255), 255, LWA_COLORKEY);

    RECT followRect = { 0 };
    GetWindowRect(followTarget, &followRect);
    cat.X = (float)((followRect.right - followRect.left) / 2 + followRect.left);
    cat.Y = (float)followRect.top;
}

void ShutdownCatWindow()
{
    if (hwndCat)
        DestroyWindow(hwndCat);
    hwndCat = 0;
    hwndCatFollow = 0;
}
