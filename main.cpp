#include <windows.h>
#include "Editor.h"

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE,
    _In_ LPWSTR lpCmdLine,
    _In_ int nShowCmd)
{
    // Чёткий рендеринг на мониторах с высоким DPI (Windows 10 1703+)
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    Editor editor;
    if (!editor.Create(hInstance, 1280, 800,
        L"FOXY"))
    {
        MessageBoxW(nullptr, L"Не удалось инициализировать Direct2D",
            L"Ошибка", MB_ICONERROR);
        return 1;
    }

    editor.Show(nShowCmd);
    return editor.Run();
    //return 0;
}