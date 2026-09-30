#pragma once
// ============================================================
// Editor.h — окно редактора
// ============================================================

#include <windows.h>
#include <windowsx.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <memory>
#include <string>
#include <commdlg.h>
#include "D2DUtils.h"
#include "Schematic.h"

#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "comdlg32.lib")

struct PaletteItem
{
    ShapeType type;
    D2D1_RECT_F rect;
    std::wstring label;
};

enum class ToolType
{
    Select = -1,
    PowerSystem = 0,
    Generator = 1,
    Transformer = 2,
    Bus = 3
};

inline ShapeType ShapeFromTool(ToolType t)
{
    return (ShapeType)t;
}



class Editor
{
public:
    bool Create(HINSTANCE hInst, int width, int height, const wchar_t* title);
    void Show(int cmdShow);
    int Run();
    HWND Handle() const { return m_hwnd; }

private:
    static LRESULT CALLBACK WndProcThunk(HWND, UINT, WPARAM, LPARAM);
    LRESULT WndProc(HWND, UINT, WPARAM, LPARAM);

    HRESULT CreateDeviceResources();
    void ReleaseDeviceResources();
    //void CreateDemoScene();

    void RenderScene();
    void DrawGrid();
    void DrawPorts();
    void DrawPendingWire();
    void DrawMarquee();
    void DrawStatus();
    void UpdateStatusText();

    void OnLButtonDown(int x, int y, bool shift, bool ctrl);
    void OnMouseMove(int x, int y, bool shift);
    void OnLButtonUp(int x, int y);
    void OnMiddleDown(int x, int y);
    void OnMiddleUp();
    void OnWheel(int delta, int clientX, int clientY);
    // void OnKeyDown(WPARAM key, bool ctrl);
    void OnKeyUp(WPARAM key);
    void OnResize();

    //-----Undo/Redo-----
    std::vector<Schematic::SnapShot> m_undoStack;
    std::vector<Schematic::SnapShot> m_redoStack;
    static constexpr size_t MAX_UNDO = 50;

    // ---- Dirty rectangles ----
    //D2D1_RECT_F m_dirtyRect{};
    //bool m_dirtyAll = true;

    // ---- Отслеживание перетаскивания для снапшота ----
    bool m_dragMoved = false;
    bool m_dragSnapshotTaken = false;

    // ---- Файл ----
    std::wstring m_currentFile;

    void PushUndo();
    void DoUndo();
    void DoRedo();
    void DoSave();
    void DoOpen();

    //void MarkAllDirty() { m_dirtyAll = true; }
    //void MarkWorldDirty(const D2D1_RECT_F& worldRect);
    void ScheduleRender() { InvalidateRect(m_hwnd, nullptr, FALSE); }
    D2D1_RECT_F StatusBarRect() const;

    void OnKeyDown(WPARAM key, bool ctrl); // +shift для Ctrl+Shift+Z

    // ---- ресурсы ----
    HWND m_hwnd = nullptr;
    HINSTANCE m_hInst = nullptr;
    ComPtr<ID2D1Factory> m_factory;
    ComPtr<ID2D1HwndRenderTarget> m_rt;
    ComPtr<ID2D1SolidColorBrush> m_brush;
    ComPtr<ID2D1StrokeStyle> m_wireStroke;
    ComPtr<ID2D1StrokeStyle> m_dashStroke;
    ComPtr<IDWriteFactory> m_dwrite;
    ComPtr<IDWriteTextFormat> m_textFormat;
    ComPtr<IDWriteTextFormat> m_centerTextFormat;

    std::unique_ptr<Schematic> m_schematic;
    d2du::ViewTransform m_view;
    d2du::GridSettings m_grid;

    // ---- интерактивное состояние ----
    int m_dragShapeIndex = -1;
    D2D1_POINT_2F m_lastDragPos{};
    bool m_panning = false;
    POINT m_lastPanPt{};
    bool m_spaceDown = false;

    struct PendingWire
    {
        bool Active = false;
        int ShapeIdx = -1;
        int PortIdx = -1;
        D2D1_POINT_2F Cursor{};
    } m_pending;

    int m_hoverShape = -1;
    int m_hoverPort = -1;

    bool m_marquee = false;
    D2D1_POINT_2F m_marqueeStart{};
    D2D1_RECT_F m_marqueeRect{};

    std::wstring m_status = L"Готово";
    D2D1_COLOR_F m_bgColor = d2du::ColorF(RGB(247, 243, 232));

    static constexpr float PORT_PICK = 10.0f; // радиус захвата клеммы, экр. px



    //---------------Палитра---------------------
    static constexpr float PALETTE_W = 104.0f;
    static constexpr float PALETTE_CELL_W = 56.0f;

    std::vector<PaletteItem> m_paletteItems;
    ToolType m_selectedTool = ToolType::Select;
    D2D1_POINT_2F m_cursorScreen{};

    void BuildPalette();
    void DrawPalette();
    void DrawPaletteIcon(const PaletteItem& it);
    bool IsPointInPalette(const D2D1_POINT_2F& p) const;
    void PlaceComponent(const D2D1_POINT_2F& worldPt);
    void DrawGhost();

    void DrawShapeLabels();

    std::pair<float, float> DefaultSize(ShapeType t) const;
    D2D1_COLOR_F DefaultColor(ShapeType t) const;

};