#include "WindowsDragDrop.h"

#include <shlobj.h>
#include <shellapi.h>
#include <memory>

namespace
{
    class OmgDropTarget : public IDropTarget
    {
    public:
        OmgDropTarget(HWND wnd, std::function<void(const std::vector<std::string>&)> cbResults, std::function<bool()> cbBusyCheck)
        {
            hwnd = wnd;
            callbackResults = cbResults;
            callbackBusyCheck = cbBusyCheck;
        }

        //IUnknown
        HRESULT __stdcall QueryInterface(REFIID iid, void **ppvObject)
        {
            if (iid == IID_IUnknown || iid == IID_IDropTarget)
            {
                *ppvObject = static_cast<IUnknown*>(this);
                AddRef();
                return S_OK;
            }

            *ppvObject = nullptr;
            return E_NOINTERFACE;
        }

        ULONG __stdcall AddRef()
        {
            return S_OK;
        }

        ULONG __stdcall Release()
        {
            return S_OK;
        }

        //IDropTarget
        HRESULT __stdcall DragEnter(IDataObject *pDataObject, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect)
        {
            isDragging = !callbackBusyCheck() && IsWhatWeWant(pDataObject);

            if (isDragging)
                *pdwEffect = DROPEFFECT_COPY;
            else
                *pdwEffect = DROPEFFECT_NONE;

            return S_OK;
        }

        HRESULT __stdcall DragOver(DWORD grfKeyState, POINTL pt, DWORD *pdwEffect)
        {
            if (isDragging)
                *pdwEffect = DROPEFFECT_COPY;
            else
                *pdwEffect = DROPEFFECT_NONE;

            return S_OK;
        }

        HRESULT __stdcall DragLeave()
        {
            isDragging = false;

            return S_OK;
        }

        HRESULT __stdcall Drop(IDataObject *pDataObject, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect)
        {
            if (!callbackBusyCheck() && isDragging && IsWhatWeWant(pDataObject))
            {
                *pdwEffect = DROPEFFECT_COPY;

                std::vector<std::string> allFileNames;

                FORMATETC fmt = { 0 };
                fmt.cfFormat = CF_HDROP;
                fmt.dwAspect = DVASPECT_CONTENT;
                fmt.lindex = -1;
                fmt.tymed = TYMED_HGLOBAL;

                STGMEDIUM stgm = { 0 };
                if (SUCCEEDED(pDataObject->GetData(&fmt, &stgm)))
                {
                    HDROP hdrop = reinterpret_cast<HDROP>(stgm.hGlobal);
                    int cFiles = DragQueryFile(hdrop, 0xFFFFFFFF, nullptr, 0);
                    for (int f = 0; f < cFiles; ++f)
                    {
                        char szFile[MAX_PATH] = { 0 };
                        int cch = DragQueryFile(hdrop, f, szFile, MAX_PATH);
                        if (cch > 0 && cch < MAX_PATH)
                            allFileNames.emplace_back(szFile);
                    }

                    ReleaseStgMedium(&stgm);
                }

                SetForegroundWindow(hwnd);

                if (!allFileNames.empty())
                    callbackResults(allFileNames);
            }
            else
                *pdwEffect = DROPEFFECT_NONE;

            return S_OK;
        }

    private:
        bool IsWhatWeWant(IDataObject *ido)
        {
            FORMATETC fmt = { 0 };
            fmt.cfFormat = CF_HDROP;
            fmt.dwAspect = DVASPECT_CONTENT;
            fmt.lindex = -1;
            fmt.tymed = TYMED_HGLOBAL;

            return ido->QueryGetData(&fmt) == S_OK;
        }

        bool isDragging = false;
        std::function<void(const std::vector<std::string>&)> callbackResults;
        std::function<bool()> callbackBusyCheck;
        HWND hwnd = 0;
    };

    std::vector<std::unique_ptr<OmgDropTarget>> allDropTargets;
}

void SetupDragDropForWindow(HWND hwnd, std::function<void(const std::vector<std::string>&)> callbackResults, std::function<bool()> callbackBusyCheck)
{
    OleInitialize(nullptr);

    allDropTargets.emplace_back(std::make_unique<OmgDropTarget>(hwnd, callbackResults, callbackBusyCheck));

    RegisterDragDrop(hwnd, allDropTargets.back().get());
}
