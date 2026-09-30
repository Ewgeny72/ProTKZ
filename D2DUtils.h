#pragma once
// ============================================================
// D2DUtils.h — вспомогательные функции для Direct2D
// Аналог модуля D2DUtils.pas
// ============================================================

#include <windows.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <algorithm>
#include <cmath>

#pragma comment(lib, "d2d1.lib")

namespace d2du
{
    // ---------- Точки и прямоугольники ----------

    inline D2D1_POINT_2F P2F(float x, float y)
    {
        D2D1_POINT_2F p; p.x = x; p.y = y; return p;
    }

    inline D2D1_RECT_F RF(float l, float t, float r, float b)
    {
        D2D1_RECT_F rc; rc.left = l; rc.top = t; rc.right = r; rc.bottom = b; return rc;
    }

    inline bool IsRectEmpty(const D2D1_RECT_F& r)
    {
        return r.right <= r.left || r.bottom <= r.top;
    }

    inline D2D1_RECT_F NormalizeRect(const D2D1_RECT_F& r)
    {
        return RF((std::min)(r.left, r.right), (std::min)(r.top, r.bottom),
            (std::max)(r.left, r.right), (std::max)(r.top, r.bottom));
    }

    // Пересекаются ли два прямоугольника
    inline bool RectsIntersect(const D2D1_RECT_F& a, const D2D1_RECT_F& b)
    {
        return a.left < b.right && a.right > b.left &&
            a.top < b.bottom && a.bottom > b.top;
    }

    inline D2D1_RECT_F UnionRect(const D2D1_RECT_F& a, const D2D1_RECT_F& b)
    {
        if (IsRectEmpty(a)) return b;
        if (IsRectEmpty(b)) return a;
        return RF((std::min)(a.left, b.left), (std::min)(a.top, b.top),
            (std::max)(a.right, b.right), (std::max)(a.bottom, b.bottom));
    }

    inline D2D1_RECT_F InflateRect(const D2D1_RECT_F& r, float pad)
    {
        return RF(r.left - pad, r.top - pad, r.right + pad, r.bottom + pad);
    }

    inline bool PointInRect(const D2D1_POINT_2F& p, const D2D1_RECT_F& r)
    {
        return p.x >= r.left && p.x <= r.right && p.y >= r.top && p.y <= r.bottom;
    }

    inline float Distance(const D2D1_POINT_2F& a, const D2D1_POINT_2F& b)
    {
        return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
    }

    // ---------- Цвет ----------

    inline D2D1_COLOR_F ColorF(COLORREF c, float alpha = 1.0f)
    {
        return D2D1::ColorF(GetRValue(c) / 255.0f,
            GetGValue(c) / 255.0f,
            GetBValue(c) / 255.0f,
            alpha);
    }

    inline D2D1_COLOR_F ColorFRGB(float r, float g, float b, float a = 1.0f)
    {
        return D2D1::ColorF(r, g, b, a);
    }

    // ---------- Матрицы ----------

    inline D2D1_MATRIX_3X2_F Matrix3x2(float m11, float m12, float m21, float m22,
        float m31, float m32)
    {
        D2D1_MATRIX_3X2_F m;
        m._11 = m11; m._12 = m12;
        m._21 = m21; m._22 = m22;
        m._31 = m31; m._32 = m32;
        return m;
    }

    inline D2D1_MATRIX_3X2_F IdentityMatrix() { return D2D1::Matrix3x2F::Identity(); }
    inline D2D1_MATRIX_3X2_F TranslationMatrix(float dx, float dy)
    {
        return D2D1::Matrix3x2F::Translation(dx, dy);
    }

    // A * B (конвенция row-vector: v' = v * M)
    inline D2D1_MATRIX_3X2_F MultiplyMatrix(const D2D1_MATRIX_3X2_F& a,
        const D2D1_MATRIX_3X2_F& b)
    {
        D2D1_MATRIX_3X2_F r;
        r._11 = a._11 * b._11 + a._12 * b._21;
        r._12 = a._11 * b._12 + a._12 * b._22;
        r._21 = a._21 * b._11 + a._22 * b._21;
        r._22 = a._21 * b._12 + a._22 * b._22;
        r._31 = a._31 * b._11 + a._32 * b._21 + b._31;
        r._32 = a._31 * b._12 + a._32 * b._22 + b._32;
        return r;
    }

    inline D2D1_POINT_2F TransformPoint(const D2D1_MATRIX_3X2_F& m, const D2D1_POINT_2F& p)
    {
        return P2F(p.x * m._11 + p.y * m._21 + m._31,
            p.x * m._12 + p.y * m._22 + m._32);
    }

    // Направление — без переноса
    inline D2D1_POINT_2F TransformDir(const D2D1_MATRIX_3X2_F& m, const D2D1_POINT_2F& v)
    {
        return P2F(v.x * m._11 + v.y * m._21,
            v.x * m._12 + v.y * m._22);
    }

    // ---------- Сетка и привязка ----------

    enum class SnapMode { None, Nodes };

    struct GridSettings
    {
        float CellSize = 20.0f;
        SnapMode Mode = SnapMode::Nodes;
        float SnapStrength = 6.0f; // радиус "магнита", 0 = всегда
        bool Enabled = true;
    };

    inline D2D1_POINT_2F SnapToGrid(const D2D1_POINT_2F& p, const GridSettings& g)
    {
        if (!g.Enabled || g.Mode == SnapMode::None || g.CellSize <= 0.0f)
            return p;

        float nx = std::round(p.x / g.CellSize) * g.CellSize;
        float ny = std::round(p.y / g.CellSize) * g.CellSize;

        if (g.SnapStrength > 0.0f)
        {
            if (std::fabs(p.x - nx) > g.SnapStrength) nx = p.x;
            if (std::fabs(p.y - ny) > g.SnapStrength) ny = p.y;
        }
        return P2F(nx, ny);
    }

    // ---------- Камера (зум + панорама) ----------

    struct ViewTransform
    {
        D2D1_POINT_2F Offset{ 0.0f, 0.0f };
        float Zoom = 1.0f;
        float MinZoom = 0.1f;
        float MaxZoom = 8.0f;

        D2D1_MATRIX_3X2_F Matrix() const
        {
            D2D1_MATRIX_3X2_F m = IdentityMatrix();
            m._11 = Zoom; m._22 = Zoom;
            m._31 = Offset.x; m._32 = Offset.y;
            return m;
        }

        D2D1_POINT_2F ScreenToWorld(const D2D1_POINT_2F& p) const
        {
            return P2F((p.x - Offset.x) / Zoom, (p.y - Offset.y) / Zoom);
        }

        D2D1_POINT_2F WorldToScreen(const D2D1_POINT_2F& p) const
        {
            return P2F(p.x * Zoom + Offset.x, p.y * Zoom + Offset.y);
        }

        void Pan(const D2D1_POINT_2F& d) { Offset.x += d.x; Offset.y += d.y; }

        void ZoomAt(const D2D1_POINT_2F& cursor, float factor)
        {
            float nz = std::clamp(Zoom * factor, MinZoom, MaxZoom);
            if (std::fabs(nz - Zoom) < 1e-4f) return;

            D2D1_POINT_2F w = ScreenToWorld(cursor);
            Zoom = nz;
            Offset.x = cursor.x - w.x * Zoom;
            Offset.y = cursor.y - w.y * Zoom;
        }

        D2D1_RECT_F WorldRectToScreen(const D2D1_RECT_F& r) const
        {
            return RF(r.left * Zoom + Offset.x, r.top * Zoom + Offset.y,
                r.right * Zoom + Offset.x, r.bottom * Zoom + Offset.y);
        }

        // Видимая область мира по размеру клиента
        D2D1_RECT_F VisibleWorldRect(int clientW, int clientH) const
        {
            D2D1_POINT_2F tl = ScreenToWorld(P2F(0.0f, 0.0f));
            D2D1_POINT_2F br = ScreenToWorld(P2F((float)clientW, (float)clientH));
            return NormalizeRect(RF(tl.x, tl.y, br.x, br.y));
        }

        D2D1_RECT_F ScreenRectToWorld(const D2D1_RECT_F& r) const
        {
            auto lt = ScreenToWorld(P2F(r.left, r.top));
            auto br = ScreenToWorld(P2F(r.right, r.bottom));
            return NormalizeRect(RF(lt.x, lt.y, br.x, br.y));
        }
    };
}