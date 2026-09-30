#include "Editor.h"
#include "resource.h"
#include <cmath>



using namespace d2du;

// Отправить объявления функций, включенных в этот модуль кода:
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);

// ==================== Создание окна ====================

bool Editor::Create(HINSTANCE hInst, int width, int height, const wchar_t* title)
{
    m_hInst = hInst;

    static const wchar_t* CLASS_NAME = L"SchematicEditorWndClass";

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProcThunk;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszMenuName = MAKEINTRESOURCE(IDR_MENU1);
    wc.hbrBackground = nullptr; // красим сами
    wc.lpszClassName = CLASS_NAME;
    if (!RegisterClassExW(&wc)) return false;

    RECT r{ 0, 0, width, height };
    AdjustWindowRectEx(&r, WS_OVERLAPPEDWINDOW, FALSE, 0);

    m_hwnd = CreateWindowExW(
        0, CLASS_NAME, title, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        r.right - r.left, r.bottom - r.top,
        nullptr, nullptr, hInst, this); // this -> GWLP_USERDATA

    if (!m_hwnd) return false;

    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
        m_factory.GetAddressOf())))
        return false;

    m_schematic = std::make_unique<Schematic>(m_factory.Get());

    if (FAILED(CreateDeviceResources())) return false;

    //CreateDemoScene();
    UpdateStatusText();
    return true;
}

void Editor::Show(int cmdShow) { ShowWindow(m_hwnd, cmdShow); }

int Editor::Run()
{
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}

HRESULT Editor::CreateDeviceResources()
{
    if (m_rt) return S_OK;

    RECT rc; GetClientRect(m_hwnd, &rc);
    const auto size = D2D1::SizeU((UINT32)(rc.right - rc.left),
        (UINT32)(rc.bottom - rc.top));

    HRESULT hr = m_factory->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(),
        D2D1::HwndRenderTargetProperties(m_hwnd, size),
        m_rt.GetAddressOf());
    if (FAILED(hr)) return hr;

    hr = m_rt->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f), &m_brush);
    if (FAILED(hr)) return hr;

    D2D1_STROKE_STYLE_PROPERTIES sp{};
    sp.startCap = D2D1_CAP_STYLE_ROUND;
    sp.endCap = D2D1_CAP_STYLE_ROUND;
    sp.dashCap = D2D1_CAP_STYLE_ROUND;
    sp.lineJoin = D2D1_LINE_JOIN_ROUND;
    sp.dashStyle = D2D1_DASH_STYLE_SOLID;

    hr = m_factory->CreateStrokeStyle(sp, nullptr, 0, &m_wireStroke);
    if (FAILED(hr)) return hr;

    sp.dashStyle = D2D1_DASH_STYLE_DASH;
    hr = m_factory->CreateStrokeStyle(sp, nullptr, 0, &m_dashStroke);
    if (FAILED(hr)) return hr;

    // DirectWrite для статусной строки
    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(m_dwrite.GetAddressOf()));
    if (SUCCEEDED(hr))
    {
        m_dwrite->CreateTextFormat(
            L"Segoe UI", nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 13.0f, L"ru-ru", &m_textFormat);

        // В CreateDeviceResources(), после m_textFormat:
        m_dwrite->CreateTextFormat(L"Segoe UI", nullptr,
            DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 26.0f, L"ru-ru", &m_centerTextFormat);
        m_centerTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        m_centerTextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        BuildPalette();
    }
    return S_OK;
}

void Editor::ReleaseDeviceResources()
{
    m_textFormat.Reset();
    m_dwrite.Reset();
    m_dashStroke.Reset();
    m_wireStroke.Reset();
    m_brush.Reset();
    m_rt.Reset();
}

// ==================== Демо-сцена ====================

/*void Editor::CreateDemoScene()
{
    // Два прямоугольных "прибора" с 4 клеммами
    m_schematic->AddShape(ShapeType::PowerSystem, 80.0f, 60.0f, P2F(200.0f, 200.0f), ColorF(RGB(000, 00, 200)));
    m_schematic->AddShape(ShapeType::PowerSystem, 80.0f, 60.0f, P2F(350.0f, 350.0f), ColorF(RGB(200, 000, 200)));

    m_schematic->AddShape(ShapeType::PowerSystem, 60, 60, P2F(500, 500), ColorF(RGB(0, 100, 10)));





    // Соединение: правая клемма 0 -> левая клемма 1
    m_schematic->AddConnection(0, 1, 1, 3, RouteKind::Straight);
}*/

// ==================== Оконная процедура ====================

LRESULT CALLBACK Editor::WndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    Editor* self = nullptr;

    if (msg == WM_NCCREATE)
    {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<Editor*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        if (self) self->m_hwnd = hwnd;
    }
    else
    {
        self = reinterpret_cast<Editor*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    return self ? self->WndProc(hwnd, msg, wp, lp)
        : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT Editor::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_COMMAND:
    {
        int wmId = LOWORD(wp);
        switch (wmId)
        {
        case IDM_ABOUT: // кнопка в меню О программе...
            DialogBox(m_hInst, MAKEINTRESOURCE(IDD_ABOUTBOX/*это диалоговое окно*/), hwnd, About);
            break;

        case IDM_EXIT:
            DestroyWindow(hwnd);
            break;

       

        default:
            return DefWindowProcW(hwnd, msg, wp, lp);
        }
        
    }   
    return 0;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        RenderScene();
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1; // гасим мерцание — красим сами

    case WM_SIZE:
        OnResize();
        return 0;

    case WM_LBUTTONDOWN:
        SetCapture(hwnd);
        OnLButtonDown(GET_X_LPARAM(lp), GET_Y_LPARAM(lp),
            (wp & MK_SHIFT) != 0, (wp & MK_CONTROL) != 0);
        return 0;

    case WM_MOUSEMOVE:
        OnMouseMove(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), (wp & MK_SHIFT) != 0);
        return 0;

    case WM_LBUTTONUP:
        ReleaseCapture();
        OnLButtonUp(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;

    case WM_MBUTTONDOWN:
        SetCapture(hwnd);
        OnMiddleDown(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;

    case WM_MBUTTONUP:
        ReleaseCapture();
        OnMiddleUp();
        return 0;

    case WM_MOUSEWHEEL:
    {
        POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) }; // ЭКРАННЫЕ координаты
        ScreenToClient(hwnd, &p);
        OnWheel(GET_WHEEL_DELTA_WPARAM(wp), p.x, p.y);
        return 0;
    }

    case WM_KEYUP:
        OnKeyUp(wp);
        return 0;

    case WM_KEYDOWN:
        OnKeyDown(wp, (GetKeyState(VK_CONTROL) & 0x8000) != 0);
        return 0;

    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT)
        {
            SetCursor(LoadCursorW(nullptr,
                (m_spaceDown || m_panning) ? IDC_SIZEALL : IDC_ARROW));
            return TRUE;
        }
        break;

    case WM_DISPLAYCHANGE:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_DESTROY:
        ReleaseDeviceResources();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// =============Обработчик сообщений для окна "О программе".

INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

// ==================== Обработчики ввода ====================

void Editor::OnResize()
{
    if (!m_rt) return;
    RECT rc; GetClientRect(m_hwnd, &rc);
    UINT32 w = (UINT32)(rc.right - rc.left), h = (UINT32)(rc.bottom - rc.top);
    if (w == 0 || h == 0) return;
    m_rt->Resize(D2D1::SizeU(w, h));
    //MarkAllDirty();
    ScheduleRender();
}

void Editor::OnLButtonDown(int x, int y, bool shift, bool ctrl)
{
    const D2D1_POINT_2F w = m_view.ScreenToWorld(P2F((float)x, (float)y));
    int si = -1, pi = -1, ci = -1;

    D2D1_POINT_2F screenPt = P2F((float)x, (float)y);

    // 1. Клик по палитре — выбор инструмента
    if (IsPointInPalette(screenPt))
    {
        for (const auto& it : m_paletteItems)
            if (PointInRect(screenPt, it.rect))
            {
                m_selectedTool = (ToolType)it.type;
                UpdateStatusText();
                ScheduleRender();
                return;
            }
        return;
    }

    // 2. Режим размещения компонента
    if (m_selectedTool != ToolType::Select)
    {
        auto world = m_view.ScreenToWorld(screenPt);
        PlaceComponent(world);
        if (!shift) m_selectedTool = ToolType::Select; // возврат к выбору
        UpdateStatusText();
        return;
    }


    // 3. Клемма — наивысший приоритет (тянем провод)
    if (m_schematic->FindPortAt(w, PORT_PICK / m_view.Zoom, si, pi))
    {
        m_pending.Active = true;
        m_pending.ShapeIdx = si;
        m_pending.PortIdx = pi;
        m_pending.Cursor = w;
        return;
    }

    // 4. Фигура — выделение + перетаскивание группы
    si = m_schematic->FindShapeAt(w);
    if (si >= 0)
    {
        if (shift)
            m_schematic->ToggleSelectShape(si);
        else if (!m_schematic->IsShapeSelected((size_t)si))
        {
            m_schematic->ClearSelection();
            m_schematic->SelectShape(si);
        }
        m_dragShapeIndex = si;
        m_lastDragPos = w;
        UpdateStatusText();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    // 5. Провод — выделение
    if (m_schematic->FindConnectionAt(w, ci))
    {
        if (shift) m_schematic->ToggleSelectConnection(ci);
        else { m_schematic->ClearSelection(); m_schematic->SelectConnection(ci); }
        UpdateStatusText();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    // 6. Пустое место — снять выделение / начать рамку
    m_schematic->ClearSelection();
    m_marquee = true;
    m_marqueeStart = w;
    m_marqueeRect = RF(w.x, w.y, w.x, w.y);
    UpdateStatusText();
    InvalidateRect(m_hwnd, nullptr, FALSE);

    if (m_pending.Active)
    {
        int si = -1, pi = -1;
        if (m_schematic->FindPortAt(w, PORT_PICK / m_view.Zoom, si, pi)
            && si != m_pending.ShapeIdx)
        {
            PushUndo(); // до добавления
            m_schematic->AddConnection(m_pending.ShapeIdx, m_pending.PortIdx,
                si, pi, RouteKind::Orthogonal);
        }
        m_pending.Active = false;
        m_hoverShape = m_hoverPort = -1;
        //MarkAllDirty();
        ScheduleRender();
    }
}

void Editor::OnMouseMove(int x, int y, bool shift)
{
    const D2D1_POINT_2F w = m_view.ScreenToWorld(P2F((float)x, (float)y));
    m_cursorScreen = P2F((float)x, (float)y);

    // Если выбран инструмент размещения — просто обновляем призрака
    if (m_selectedTool != ToolType::Select)
    {
        ScheduleRender();
        return;
    }

    // Панорама
    if (m_panning)
    {
        m_view.Pan(P2F((float)(x - m_lastPanPt.x), (float)(y - m_lastPanPt.y)));
        m_lastPanPt = { x, y };
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    // Протяжка провода
    if (m_pending.Active)
    {
        m_pending.Cursor = w;
        int si = -1, pi = -1;
        if (m_schematic->FindPortAt(w, PORT_PICK / m_view.Zoom, si, pi)
            && si != m_pending.ShapeIdx)
        {
            m_hoverShape = si; m_hoverPort = pi;
        }
        else
        {
            m_hoverShape = -1; m_hoverPort = -1;
        }

        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    // Рамка выделения
    if (m_marquee)
    {
        m_marqueeRect = NormalizeRect(RF(m_marqueeStart.x, m_marqueeStart.y, w.x, w.y));
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    // Перетаскивание группы фигур
    if (m_dragShapeIndex >= 0)
    {
        D2D1_POINT_2F delta = P2F(w.x - m_lastDragPos.x, w.y - m_lastDragPos.y);

        // Привязка к сетке применяется к дельте, а не к абсолютной позиции,
        // иначе группа "слипнется" в один узел
        if (m_grid.Enabled)
        {
            auto snapped = SnapToGrid(
                P2F(m_schematic->GetShapePosition(m_dragShapeIndex).x + delta.x,
                    m_schematic->GetShapePosition(m_dragShapeIndex).y + delta.y),
                m_grid);
            auto cur = m_schematic->GetShapePosition(m_dragShapeIndex);
            delta = P2F(snapped.x - cur.x, snapped.y - cur.y);
        }

        m_schematic->MoveSelectedShapes(delta);
        m_lastDragPos = P2F(m_lastDragPos.x + delta.x, m_lastDragPos.y + delta.y);
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }




}

void Editor::OnLButtonUp(int x, int y)
{
    const D2D1_POINT_2F w = m_view.ScreenToWorld(P2F((float)x, (float)y));

    // Завершение протяжки провода
    if (m_pending.Active)
    {
        int si = -1, pi = -1;
        if (m_schematic->FindPortAt(w, PORT_PICK / m_view.Zoom, si, pi)
            && si != m_pending.ShapeIdx)
        {
            m_schematic->AddConnection(m_pending.ShapeIdx, m_pending.PortIdx,
                si, pi, RouteKind::Orthogonal);
        }
        m_pending.Active = false;
        m_hoverShape = m_hoverPort = -1;
        UpdateStatusText();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    // Завершение рамки
    if (m_marquee)
    {
        m_marquee = false;
        if (std::fabs(m_marqueeRect.right - m_marqueeRect.left) > 2.0f ||
            std::fabs(m_marqueeRect.bottom - m_marqueeRect.top) > 2.0f)
        {
            m_schematic->SelectShapesInRect(m_marqueeRect);
            m_schematic->SelectConnectionsInRect(m_marqueeRect);
        }
        UpdateStatusText();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    /*if (m_pending.Active)
    {
        int si = -1, pi = -1;
        if (m_schematic->FindPortAt(w, PORT_PICK / m_view.Zoom, si, pi)
            && si != m_pending.ShapeIdx)
        {
            PushUndo(); // до добавления
            m_schematic->AddConnection(m_pending.ShapeIdx, m_pending.PortIdx,
                si, pi, RouteKind::Orthogonal);
        }
        m_pending.Active = false;
        m_hoverShape = m_hoverPort = -1;
        //MarkAllDirty();
        ScheduleRender();
    }*/

    m_dragShapeIndex = -1;
}

void Editor::OnMiddleDown(int x, int y)
{
    m_panning = true;
    m_lastPanPt = { x, y };
}

void Editor::OnMiddleUp()
{
    m_panning = false; 
}


void Editor::OnWheel(int delta, int clientX, int clientY)
{
    const float factor = (delta > 0) ? 1.10f : 1.0f / 1.10f;
    m_view.ZoomAt(P2F((float)clientX, (float)clientY), factor);
    UpdateStatusText();
    InvalidateRect(m_hwnd, nullptr, FALSE);

}

void Editor::OnKeyDown(WPARAM key, bool ctrl)
{
    switch (key)
    {
    case VK_SPACE:
        if (!m_spaceDown) { m_spaceDown = true; }
        break;

    case VK_DELETE:
        if (m_schematic->HasSelection())
        {
            PushUndo(); // до удаления
            if (m_schematic->SelectedShapeCount() > 0)
                m_schematic->DeleteSelectedShapes();
            else
                m_schematic->DeleteSelectedConnections();
            m_dragShapeIndex = -1;
            UpdateStatusText();
           // MarkAllDirty();
            ScheduleRender();
        }
        break;

    case VK_ESCAPE:
        m_selectedTool = ToolType::Select; // выход из режима размещения
        m_schematic->ClearSelection();
        m_marquee = false;
        m_pending.Active = false;
        UpdateStatusText();
        //MarkAllDirty();
        ScheduleRender();
        break;

    case 'Z':
        if (ctrl) DoUndo(); // Ctrl+Z / Ctrl+Shift+Z
        break;


    case 'Y':
        if (ctrl) DoRedo(); // Ctrl+Y
        break;

    case 'S':
        if (ctrl) DoSave(); // Ctrl+S
        break;

    case 'O':
        if (ctrl) DoOpen(); // Ctrl+O
        break;

    case 'A':
        if (ctrl)
        {
            m_schematic->SelectAll();
            UpdateStatusText();
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        break;
    }
}

void Editor::OnKeyUp(WPARAM key)
{
    if (key == VK_SPACE) m_spaceDown = false;
}

// ==================== Отрисовка ====================

void Editor::RenderScene()
{
    if (!m_rt || !m_schematic) return;

    m_rt->BeginDraw();
    m_rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    m_rt->SetTransform(IdentityMatrix());
    m_rt->Clear(m_bgColor);

    m_rt->SetTransform(m_view.Matrix());
    DrawGrid();

    // Модель
    m_schematic->Render(m_rt.Get(), m_brush.Get(), m_wireStroke.Get(), m_view.Matrix());

    DrawShapeLabels();

    // Интерактивные слои (матрица вида уже установлена)
    DrawPorts();
    DrawPendingWire();
    DrawMarquee();
    DrawGhost();

    // HUD в экранных координатах
    m_rt->SetTransform(IdentityMatrix());
    DrawStatus();
    DrawPalette();

    HRESULT hr = m_rt->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET)
    {
        ReleaseDeviceResources();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void Editor::DrawGrid()
{
    if (!m_grid.Enabled) return;

    const float cell = m_grid.CellSize;
    RECT rc; GetClientRect(m_hwnd, &rc);
    const D2D1_RECT_F vis = m_view.VisibleWorldRect(rc.right - rc.left, rc.bottom - rc.top);

    const float lw = 1.0f / m_view.Zoom; // 1 экранный пиксель

    m_brush->SetColor(ColorFRGB(0.78f, 0.74f, 0.66f, 0.35f));

    m_rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);

    float startX = std::floor(vis.left / cell) * cell;
    for (float x = startX; x <= vis.right; x += cell)
        m_rt->DrawLine(P2F(x, vis.top), P2F(x, vis.bottom), m_brush.Get(), lw);

    float startY = std::floor(vis.top / cell) * cell;
    for (float y = startY; y <= vis.bottom; y += cell)
        m_rt->DrawLine(P2F(vis.left, y), P2F(vis.right, y), m_brush.Get(), lw);

    m_rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
}

void Editor::DrawPorts()
{
    const float r = 4.5f; /*    / m_view.Zoom*/ // постоянный размер на экране/ убрал постоянный размер порта

    for (int i = 0; i < (int)m_schematic->ShapeCount(); ++i)
    {
        auto pts = m_schematic->GetPortPositions(i);
        for (const auto& p : pts)
        {
            D2D1_ELLIPSE e = D2D1::Ellipse(p, r, r);

            m_brush->SetColor(ColorF(RGB(184, 115, 51)));
            m_rt->FillEllipse(e, m_brush.Get());

            m_brush->SetColor(ColorF(RGB(62, 44, 28)));
            m_rt->DrawEllipse(e, m_brush.Get(), 1.0f / m_view.Zoom);
        }
    }

    // Подсветка клеммы под курсором
    if (m_hoverShape >= 0 && m_hoverPort >= 0)
    {
        auto p = m_schematic->GetPortWorld(m_hoverShape, m_hoverPort);
        D2D1_ELLIPSE glow = D2D1::Ellipse(p, r * 2.2f, r * 2.2f);
        m_brush->SetColor(ColorFRGB(1.0f, 0.84f, 0.0f, 0.35f));
        m_rt->FillEllipse(glow, m_brush.Get());
    }
}

void Editor::DrawPendingWire()
{
    if (!m_pending.Active) return;

    const D2D1_POINT_2F a = m_schematic->GetPortWorld(m_pending.ShapeIdx, m_pending.PortIdx);
    const D2D1_POINT_2F b = m_pending.Cursor;

    m_brush->SetColor(ColorFRGB(0.72f, 0.45f, 0.17f, 0.85f));
    m_rt->DrawLine(a, b, m_brush.Get(), 2.0f / m_view.Zoom, m_dashStroke.Get());
}

void Editor::DrawMarquee()
{
    if (!m_marquee) return;

    m_brush->SetColor(ColorFRGB(1.0f, 0.84f, 0.0f, 0.10f));
    m_rt->FillRectangle(m_marqueeRect, m_brush.Get());

    m_brush->SetColor(ColorFRGB(1.0f, 0.84f, 0.0f, 0.80f));
    m_rt->DrawRectangle(m_marqueeRect, m_brush.Get(),
        1.0f / m_view.Zoom, m_dashStroke.Get());
}

void Editor::UpdateStatusText()
{
    wchar_t buf[256];
    const int ns = m_schematic ? m_schematic->SelectedShapeCount() : 0;
    const int nc = m_schematic ? m_schematic->SelectedConnectionCount() : 0;

    if (ns || nc)
        swprintf_s(buf, L"Выбрано: фигур %d, соединений %d | Delete — удалить | "
            L"Зум %.0f%%", ns, nc, m_view.Zoom * 100.0f);
    else
        swprintf_s(buf, L"Готов | ЛКМ — выбор/тянуть клемму | СКМ — панорама | "
            L"колёсико — zoom | Zoom %.0f%%", m_view.Zoom * 100.0f);
    m_status = buf;
}

void Editor::DrawStatus()
{
    if (!m_textFormat) return;

    RECT rc; GetClientRect(m_hwnd, &rc);
    D2D1_RECT_F box = RF(8.0f, (float)rc.bottom - 26.0f, (float)rc.right - 8.0f, (float)rc.bottom - 4.0f);

    m_brush->SetColor(ColorFRGB(0.15f, 0.10f, 0.05f, 0.75f));
    m_rt->FillRectangle(box, m_brush.Get());

    m_brush->SetColor(ColorFRGB(0.95f, 0.88f, 0.70f, 1.0f));
    m_rt->DrawTextW(m_status.c_str(), (UINT32)m_status.size(),
        m_textFormat.Get(), box, m_brush.Get());
}

/*void Editor::MarkWorldDirty(const D2D1_RECT_F& worldRect)
{
    if (m_dirtyAll) return;
    auto screenRect = m_view.WorldRectToScreen(worldRect);
    screenRect = InflateRect(screenRect, 4.0f); // запас на толщину линий и сглаживание
    m_dirtyRect = UnionRect(m_dirtyRect, screenRect);
}*/

D2D1_RECT_F Editor::StatusBarRect() const
{
    RECT rc; GetClientRect(m_hwnd, &rc);
    return RF(0.0f, (float)rc.bottom - 30.0f, (float)rc.right, (float)rc.bottom);
}

void Editor::PushUndo()
{
    m_undoStack.push_back(m_schematic->TakeSnapshot());
    m_redoStack.clear(); // новое действие сбрасывает историю повторов
    if (m_undoStack.size() > MAX_UNDO)
        m_undoStack.erase(m_undoStack.begin());
}

void Editor::DoUndo()
{
    if (m_undoStack.empty()) return;
    m_redoStack.push_back(m_schematic->TakeSnapshot());
    m_schematic->RestoreSnapshot(m_undoStack.back());
    m_undoStack.pop_back();

    m_dragShapeIndex = -1;
    m_pending.Active = false;
    m_marquee = false;

    UpdateStatusText();
    //MarkAllDirty();
    ScheduleRender();
}

void Editor::DoRedo()
{
    if (m_redoStack.empty()) return;
    m_undoStack.push_back(m_schematic->TakeSnapshot());
    m_schematic->RestoreSnapshot(m_redoStack.back());
    m_redoStack.pop_back();

    UpdateStatusText();
   // MarkAllDirty();
    ScheduleRender();
}

void Editor::DoSave()
{
    wchar_t fileBuf[MAX_PATH] = L"scheme.sch";
    if (!m_currentFile.empty())
        wcsncpy_s(fileBuf, m_currentFile.c_str(), _TRUNCATE);

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = L"Схема (*.sch)\0*.sch\0Все файлы (*.*)\0*.*\0";
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"sch";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;

    if (GetSaveFileNameW(&ofn))
    {
        m_currentFile = fileBuf;
        if (m_schematic->SaveToFile(m_currentFile))
            m_status = L"Сохранено: " + m_currentFile;
        else
            m_status = L"Ошибка сохранения файла";
        ScheduleRender();
    }
}

void Editor::DoOpen()
{
    wchar_t fileBuf[MAX_PATH] = L"";

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = L"Схема (*.sch)\0*.sch\0Все файлы (*.*)\0*.*\0";
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"sch";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (GetOpenFileNameW(&ofn))
    {
        m_currentFile = fileBuf;
        if (m_schematic->LoadFromFile(m_currentFile))
        {
            m_undoStack.clear();
            m_redoStack.clear();
            m_dragShapeIndex = -1;
            m_pending.Active = false;
            m_status = L"Открыто: " + m_currentFile;
            //MarkAllDirty();
            ScheduleRender();
        }
        else
        {
            m_status = L"Ошибка чтения файла (неверный формат?)";
            ScheduleRender();
        }
    }


}

void Editor::BuildPalette()
{
    m_paletteItems.clear();
    float y = 10.0f;
    auto add = [&](ShapeType t, const wchar_t* label)
        {
            PaletteItem it;
            it.type = t;
            it.rect = RF(8.0f, y, PALETTE_W - 8.0f, y + PALETTE_CELL_W);
            it.label = label;
            m_paletteItems.push_back(it);
            y += PALETTE_CELL_W + 6.0f;
        };

    add(ShapeType::PowerSystem, L"      Система");
    add(ShapeType::Generator, L"   Генератор");
    add(ShapeType::Transformer2w, L"  Тр-р 2х. обм.");
    add(ShapeType::Bus, L"       Шина");
}

bool Editor::IsPointInPalette(const D2D1_POINT_2F& p) const
{
    return p.x >= 0 && p.x <= PALETTE_W;
}

std::pair<float, float> Editor::DefaultSize(ShapeType t) const
{
    switch (t)
    {
    case ShapeType::PowerSystem: return { 80.0f, 60.0f };
    case ShapeType::Generator: return { 70.0f, 70.0f };  
    case ShapeType::Transformer2w: return { 90.0f, 100.0f };
    case ShapeType::Bus: return { 200.0f, 10.0f };
    }
    return { 80.0f, 60.0f };
}

D2D1_COLOR_F Editor::DefaultColor(ShapeType t) const
{
    switch (t)
    {
    case ShapeType::Generator: return ColorF(RGB(184, 115, 51));
    case ShapeType::Transformer2w: return ColorF(RGB(160, 95, 45));
    case ShapeType::Bus: return ColorF(RGB(130, 130, 140)); // сталь
    default: return ColorF(RGB(150, 110, 70));
    }
}

void Editor::DrawPalette()
{
    RECT rc; GetClientRect(m_hwnd, &rc);
    D2D1_RECT_F panel = RF(0, 0, PALETTE_W, (float)rc.bottom - 30);

    m_brush->SetColor(ColorFRGB(0.16f, 0.12f, 0.09f, 0.65f /*прозрачность палитры*/));
    m_rt->FillRectangle(panel, m_brush.Get());

    // Медная кромка палитры
    //m_brush->SetColor(ColorFRGB(0.72f, 0.45f, 0.17f, 0.7f));
    //m_rt->DrawLine(P2F(PALETTE_W, 0), P2F(PALETTE_W, (float)rc.bottom), m_brush.Get(), 2.0f);  //Кромка палитры

    for (const auto& it : m_paletteItems)
    {
        bool active = ((int)m_selectedTool == (int)it.type);
        m_brush->SetColor(active ? ColorFRGB(1.0f, 0.84f, 0.0f, 0.22f)
            : ColorFRGB(0.26f, 0.20f, 0.14f, 0.9f));
        m_rt->FillRectangle(it.rect, m_brush.Get());

        m_brush->SetColor(active ? ColorFRGB(1.0f, 0.84f, 0.0f, 0.9f)
            : ColorFRGB(0.72f, 0.45f, 0.17f, 0.6f));
        m_rt->DrawRectangle(it.rect, m_brush.Get(), 1.2f);

        DrawPaletteIcon(it);

        // Подпись
        if (m_textFormat)
        {
            D2D1_RECT_F labelBox = RF(it.rect.left, it.rect.bottom - 16.0f,
                it.rect.right, it.rect.bottom - 2.0f);
            m_brush->SetColor(ColorFRGB(0.9f, 0.82f, 0.65f, 1.0f));
            m_rt->DrawTextW(it.label.c_str(), (UINT32)it.label.size(),
                m_textFormat.Get(), labelBox, m_brush.Get());
        }
    }
}

void Editor::DrawPaletteIcon(const PaletteItem& it)
{
    float cx = (it.rect.left + it.rect.right) / 2.0f;
    float cy = it.rect.top + 20.0f;
    m_brush->SetColor(ColorFRGB(0.85f, 0.65f, 0.4f, 1.0f));

    switch (it.type)
    {
    case ShapeType::PowerSystem:
        m_rt->DrawRectangle(RF(cx - 10, cy - 10, cx + 10, cy + 10), m_brush.Get(), 1.5f);
        m_rt->DrawLine(P2F(cx, cy + 10), P2F(cx, cy + 20), m_brush.Get(), 1.5f);
        break;
    case ShapeType::Generator:
        m_rt->DrawEllipse(D2D1::Ellipse(P2F(cx, cy), 10, 10), m_brush.Get(), 1.5f);
        m_rt->DrawLine(P2F(cx, cy + 10), P2F(cx, cy + 20), m_brush.Get(), 1.5f);
        break;
    case ShapeType::Transformer2w:
        // Две обмотки кругами
        m_rt->DrawEllipse(D2D1::Ellipse(P2F(cx, cy - 2), 10, 10), m_brush.Get(), 1.5f);
        m_rt->DrawEllipse(D2D1::Ellipse(P2F(cx, cy + 10), 10, 10), m_brush.Get(), 1.5f);


        break;
    case ShapeType::Bus:
        m_rt->DrawLine(P2F(cx - 16, cy), P2F(cx + 16, cy), m_brush.Get(), 3.5f);
        break;
    }
}

void Editor::PlaceComponent(const D2D1_POINT_2F& worldPt)
{
    auto st = ShapeFromTool(m_selectedTool);
    auto [w, h] = DefaultSize(st);

    PushUndo();
    m_schematic->AddShape(st, w, h, worldPt, DefaultColor(st));

    //MarkAllDirty();
    ScheduleRender();
}

void Editor::DrawGhost()
{
    if (m_selectedTool == ToolType::Select) return;
    if (IsPointInPalette(m_cursorScreen)) return;

    auto st = ShapeFromTool(m_selectedTool);
    auto [w, h] = DefaultSize(st);
    auto world = m_view.ScreenToWorld(m_cursorScreen);

    auto m = MultiplyMatrix(TranslationMatrix(world.x, world.y), m_view.Matrix());
    m_rt->SetTransform(m);

    m_brush->SetColor(ColorFRGB(1.0f, 0.84f, 0.0f, 0.4f));
    if (st == ShapeType::PowerSystem || st == ShapeType::Bus || st == ShapeType::Transformer2w)
        m_rt->DrawRectangle(RF(-w / 2, -h / 2, w / 2, h / 2), m_brush.Get(), 1.5f, m_dashStroke.Get());
    else
        m_rt->DrawEllipse(D2D1::Ellipse(P2F(0, 0), w / 2, h / 2), m_brush.Get(), 1.5f, m_dashStroke.Get());

    m_rt->SetTransform(m_view.Matrix());
}

//*********************************************************
//---------- Текст в элементах схемы ----------------------

void Editor::DrawShapeLabels()
{
    if (!m_centerTextFormat || !m_schematic) return;

    for (size_t i = 0; i < m_schematic->ShapeCount(); ++i)
    {
        if (m_schematic->GetShapeType(i) != ShapeType::Generator) continue;
        auto m = MultiplyMatrix(m_schematic->GetShapeMatrix(i), m_view.Matrix());
        m_rt->SetTransform(m);
        D2D1_RECT_F box = RF(-18.0f, -15.0f, 18.0f, 15.0f);
        m_brush->SetColor(ColorFRGB(0.12f, 0.07f, 0.02f, 0.9f));
        m_rt->DrawTextW(L"G", 1, m_centerTextFormat.Get(), box, m_brush.Get());
    }

    for (size_t i = 0; i < m_schematic->ShapeCount(); ++i)
    {
        if (m_schematic->GetShapeType(i) != ShapeType::PowerSystem) continue;
        auto m = MultiplyMatrix(m_schematic->GetShapeMatrix(i), m_view.Matrix());
        m_rt->SetTransform(m);
        D2D1_RECT_F box = RF(-18.0f, -15.0f, 18.0f, 15.0f);
        m_brush->SetColor(ColorFRGB(0.12f, 0.07f, 0.02f, 0.9f));
        m_rt->DrawTextW(L"Sys", 1, m_centerTextFormat.Get(), box, m_brush.Get());
    }


    m_rt->SetTransform(m_view.Matrix());
}