#pragma once
// ============================================================
// Schematic.h — модель схемы
// Фигуры, клеммы, соединения, маршрутизация, выделение
// ============================================================

#include <windows.h>
#include <d2d1.h>
#include <vector>
#include <string>
#include <unordered_map>
#include <wrl/client.h>
#include "D2DUtils.h"

using Microsoft::WRL::ComPtr;


//---------Тип фигуры-------------

enum class ShapeType : int32_t
{
    PowerSystem = 0,
    Generator = 1,
    Transformer2w = 2,
    Bus = 3
};





// ---------- Типы данных ----------

enum class RouteKind { Straight, Orthogonal, Bezier };

struct Port
{
    D2D1_POINT_2F LocalPos{ 0.0f, 0.0f }; // положение в локальных координатах
    D2D1_POINT_2F Normal{ 1.0f, 0.0f }; // направление вылета провода
};

inline Port NewPort(const D2D1_POINT_2F& localPos, const D2D1_POINT_2F& normal)
{
    Port p; p.LocalPos = localPos; p.Normal = normal; return p;
}

// 4 стандартные клеммы прямоугольного прибора W x H
inline std::vector<Port> MakePowerSystemPorts(float w, float h)
{
    return {
        //NewPort(d2du::P2F(0.0f, -h / 2), d2du::P2F(0.0f, -1.0f)), // 0: верх
        //NewPort(d2du::P2F(w / 2, 0.0f), d2du::P2F(1.0f, 0.0f)), // 1: право
        NewPort(d2du::P2F(0.0f, h - 15.0f), d2du::P2F(0.0f, 1.0f)), // 2: низ
        //NewPort(d2du::P2F(-w / 2, 0.0f), d2du::P2F(-1.0f, 0.0f)) // 3: лево
    };
}

// Клеммы по периметру эллипса
inline std::vector<Port> MakeGeneratorPorts(float rx, float ry)
{
    return {

        NewPort(d2du::P2F(0.0f, ry + 10.0f), d2du::P2F(0.0f, 2.0f))
    };
}



// Трансформатор 2-х обмоточный: 2 клеммы первичной сверху и снизу
inline std::vector<Port> MakeTransformer2wPorts(float w, float h)
{
    //float x1 = -w / 2 ; // первичная
    //float x2 = w / 2; // вторичная
    return {
        NewPort(d2du::P2F(0, -h + 20.0f), d2du::P2F(0.0f, -1.0f)), // 0: верх первичной
        NewPort(d2du::P2F(0, h - 20.0f), d2du::P2F(0.0f, 1.0f)), // 1: низ первичной
        //NewPort(d2du::P2F(x2, -h / 2), d2du::P2F(0.0f, -1.0f)), // 2: верх вторичной
        //NewPort(d2du::P2F(x2, h / 2), d2du::P2F(0.0f, 1.0f)) // 3: низ вторичной
    };
}

// Шина: отводы сверху и снизу + торцы
inline std::vector<Port> MakeBusPorts(float w, float h, int taps)
{
    std::vector<Port> ports;
    float step = w / (taps + 1);
    for (int i = 0; i < taps; ++i)
    {
        float x = -w / 2 + step * (i + 1);
        ports.push_back(NewPort(d2du::P2F(x, -h / 2), d2du::P2F(0, -1))); // верх
        ports.push_back(NewPort(d2du::P2F(x, h / 2), d2du::P2F(0, 1))); // низ
    }
    ports.push_back(NewPort(d2du::P2F(-w / 2, 0), d2du::P2F(-1, 0))); // левый торец
    ports.push_back(NewPort(d2du::P2F(w / 2, 0), d2du::P2F(1, 0))); // правый торец
    return ports;
}

struct Shape
{
    int Id = 0;
    ShapeType Type = ShapeType::PowerSystem;
    float W = 0, H = 0; // параметры геометрии
    ComPtr<ID2D1Geometry> Geometry;
    D2D1_POINT_2F Position{ 0, 0 };
    float AngleDeg = 0.0f;
    D2D1_COLOR_F Color{};
    std::vector<Port> Ports;
    bool Selected = false;

    D2D1_MATRIX_3X2_F Matrix() const;
    D2D1_POINT_2F PortWorld(int idx) const;
    D2D1_POINT_2F PortNormalWorld(int idx) const;
    int NearestPort(const D2D1_POINT_2F& w, float threshold) const;
    bool HitTestWorld(const D2D1_POINT_2F& w) const;
    D2D1_RECT_F WorldBounds() const;
};



struct Connection
{
    int Id = 0;
    int FromShapeId = 0, FromPort = 0;
    int ToShapeId = 0, ToPort = 0;
    D2D1_COLOR_F Color{};
    float Width = 2.5f;
    RouteKind Route = RouteKind::Orthogonal;

    ComPtr<ID2D1PathGeometry> Path; // кэш геометрии
    D2D1_RECT_F Bounds{}; // мировые границы для dirty rect
    bool Dirty = true;
    bool Selected = false;
};

// ---------- Класс модели ----------

class Schematic
{
public:
    explicit Schematic(ID2D1Factory* factory); // фабрика НЕ передаётся во владение
    ~Schematic();

    
    
    //---------------UNDO/REDO-----------------------------
    struct SnapShot
    {
        std::vector<Shape> shapes;
        std::vector<Connection> connection;
    };
    SnapShot TakeSnapshot() const;
    void RestoreSnapshot(const SnapShot& s);

   
    
    //------------Границы для dirty rect-------------------
    //D2D1_RECT_F SelectionWorldBounds() const;
    //D2D1_RECT_F EstimateWireBounds(const Connection& c) const;

  
    
    //------------Файл----------------------------------
    bool SaveToFile(const std::wstring& path) const;
    bool LoadFromFile(const std::wstring& path);


    // Фигуры

    int AddShape(ShapeType Type, float w, float h, const D2D1_POINT_2F& pos, const D2D1_COLOR_F& color); //Универсальный констуктор фигур
    void DeleteShape(size_t index);
    void MoveShape(size_t index, const D2D1_POINT_2F& newPos);
    ShapeType GetShapeType(size_t i) const;
    D2D1_MATRIX_3X2_F GetShapeMatrix(size_t i) const;
    size_t ShapeCount() const { return m_shapes.size(); }
    D2D1_POINT_2F GetShapePosition(size_t i) const;
    bool IsShapeSelected(size_t i) const;
    int FindShapeAt(const D2D1_POINT_2F& w) const;

    // Клеммы
    bool FindPortAt(const D2D1_POINT_2F& w, float radius,
        int& shapeIdx, int& portIdx) const;
    D2D1_POINT_2F GetPortWorld(int shapeIdx, int portIdx) const;
    D2D1_POINT_2F GetPortNormalWorld(int shapeIdx, int portIdx) const;
    std::vector<D2D1_POINT_2F> GetPortPositions(int shapeIdx) const;

    // Соединения
    int AddConnection(int fromShapeIdx, int fromPort,
        int toShapeIdx, int toPort, RouteKind route);
    size_t ConnectionCount() const { return m_connections.size(); }
    bool FindConnectionAt(const D2D1_POINT_2F& w, int& index) const;

    // Выделение
    void SelectShape(int index);
    void ToggleSelectShape(int index);
    void SelectConnection(int index);
    void ToggleSelectConnection(int index);
    void ClearSelection();
    void DeleteSelectedShapes();
    void DeleteSelectedConnections();
    void SelectShapesInRect(const D2D1_RECT_F& r);
    void SelectConnectionsInRect(const D2D1_RECT_F& r);
    void SelectAll();
    void MoveSelectedShapes(const D2D1_POINT_2F& delta);
    int SelectedShapeCount() const;
    int SelectedConnectionCount() const;
    bool HasSelection() const;

    // Отрисовка модели
    void Render(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush,
        ID2D1StrokeStyle* wireStroke, const D2D1_MATRIX_3X2_F& viewMatrix);

    std::wstring DebugInfo() const;

private:
    int IndexOfShape(int id) const;
    void RebuildIndex();
    void MarkWiresDirty(int shapeId);
    void RebuildWire(Connection& c);

    ComPtr<ID2D1PathGeometry> BuildWirePath(const Connection& c) const;
    ComPtr<ID2D1PathGeometry> BuildPolyline(const std::vector<D2D1_POINT_2F>& pts) const;
    ComPtr<ID2D1PathGeometry> BuildBezier(const D2D1_POINT_2F& a, const D2D1_POINT_2F& an,
        const D2D1_POINT_2F& b, const D2D1_POINT_2F& bn) const;
    ComPtr<ID2D1PathGeometry> BuildOrthogonal(const D2D1_POINT_2F& a, const D2D1_POINT_2F& an,
        const D2D1_POINT_2F& b, const D2D1_POINT_2F& bn,
        float stub) const;

    void DrawWire(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush,
        ID2D1StrokeStyle* stroke, size_t i);
    void DrawWireSelected(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush,
        ID2D1StrokeStyle* stroke, size_t i);
    void DrawSelectionFrame(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush,
        ID2D1StrokeStyle* stroke, const Shape& s, float zoomHint);

    ID2D1Factory* m_factory = nullptr;
    std::vector<Shape> m_shapes;
    std::vector<Connection> m_connections;
    std::unordered_map<int, int> m_shapeIndex; // Id -> индекс
    int m_nextId = 1;


//Геометрия оборудования
    ComPtr<ID2D1Geometry> BuildGeneratorGeometry(float d) const;
    ComPtr<ID2D1Geometry> BuildTransformer2wGeometry(float w, float h) const;
    ComPtr<ID2D1Geometry> BuildBusGeometry(float w, float h) const;
    //void AddCoil(ID2D1GeometrySink* sink, float x, float yTop, float r, int n, bool bulgeRight) const;
};