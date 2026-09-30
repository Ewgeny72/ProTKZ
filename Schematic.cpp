#include "Schematic.h"
#include <unordered_set>
#include <cmath>
#include <fstream>
#include <cstring>

using namespace d2du;

// ==================== Shape ====================

D2D1_MATRIX_3X2_F Shape::Matrix() const
{
    auto t = D2D1::Matrix3x2F::Translation(Position.x, Position.y);
    if (std::fabs(AngleDeg) > 1e-4f)
        return D2D1::Matrix3x2F::Rotation(AngleDeg) * t;
    return t;
}

D2D1_POINT_2F Shape::PortWorld(int idx) const
{
    if (idx < 0 || idx >= (int)Ports.size()) return Position;
    return TransformPoint(Matrix(), Ports[idx].LocalPos);
}

D2D1_POINT_2F Shape::PortNormalWorld(int idx) const
{
    if (idx < 0 || idx >= (int)Ports.size()) return P2F(1.0f, 0.0f);
    return TransformDir(Matrix(), Ports[idx].Normal);
}

int Shape::NearestPort(const D2D1_POINT_2F& w, float threshold) const
{
    int best = -1;
    float bestD = threshold;
    for (int i = 0; i < (int)Ports.size(); ++i)
    {
        float d = Distance(w, PortWorld(i));
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

bool Shape::HitTestWorld(const D2D1_POINT_2F& w) const
{
    if (!Geometry) return false;

    // Передаём МИРОВУЮ матрицу фигуры — Direct2D сам приведёт точку
    // к локальным координатам геометрии. Корректно работает и при повороте.
    auto m = Matrix();
    BOOL contains = FALSE;
    if (SUCCEEDED(Geometry->FillContainsPoint(w, &m, 0.25f, &contains)))
        return contains != FALSE;
    return false;
}

D2D1_RECT_F Shape::WorldBounds() const
{
    if (!Geometry) return RF(0, 0, 0, 0);
    auto m = Matrix();
    D2D1_RECT_F b{};
    if (FAILED(Geometry->GetBounds(&m, &b))) return RF(0, 0, 0, 0);
    return b;
}

//---------------------Undo/Redo Снапшоты--------------
Schematic::SnapShot Schematic::TakeSnapshot() const
{
    //ComPtr копируется счетчик ссылок - геометрия не копируется
    return SnapShot{ m_shapes, m_connections };
}

void Schematic::RestoreSnapshot(const SnapShot& s)
{
    m_shapes = s.shapes;
    m_connections = s.connection;
    RebuildIndex();
    for (auto& c : m_connections)
    {
        c.Path.Reset(); //пересчитываем пути в мировых координатах
        c.Dirty = true;
    }
}

//-------------------Границы выделения для dirty rect--------------
/*D2D1_RECT_F Schematic::EstimateWireBounds(const Connection& c) const
{
    int fi = IndexOfShape(c.FromShapeId);
    int ti = IndexOfShape(c.ToShapeId);
    if (fi < 0 || ti < 0) return RF(0, 0, 0, 0);

    auto a = m_shapes[fi].PortWorld(c.FromPort);
    auto b = m_shapes[ti].PortWorld(c.ToPort);

    // +20 пикселей запаса под ортогональные изгибы
    return InflateRect(NormalizeRect(RF(a.x, a.y, b.x, b.y)), c.Width + 20.0f);
}

D2D1_RECT_F Schematic::SelectionWorldBounds() const
{
    std::unordered_set<int> selIds;
    for (const auto& s : m_shapes)
        if (s.Selected) selIds.insert(s.Id);

    D2D1_RECT_F acc{};
    bool has = false;

    for (const auto& s : m_shapes)
        if (s.Selected)
        {
            acc = has ? UnionRect(acc, s.WorldBounds()) : s.WorldBounds();
            has = true;
        }

    for (const auto& c : m_connections)
        if (selIds.count(c.FromShapeId) || selIds.count(c.ToShapeId))
        {
            auto b = c.Dirty ? EstimateWireBounds(c) : c.Bounds;
            acc = has ? UnionRect(acc, b) : b;
            has = true;
        }

    return acc;
}*/

//*********************************************************
//----------Сохранение/загрузка (бинарный формат)------
//*********************************************************
#pragma pack(push, 1)
struct FileHeader { char magic[5]; int32_t version; };
struct FileShape
{
    int32_t id, type;
    float x, y, angle;
    float r, g, b, a;
    float w, h;
    int32_t portCount;
};
struct FilePort { float lx, ly, nx, ny; };
struct FileConn
{
    int32_t id, fromId, fromPort, toId, toPort;
    float r, g, b, a, width;
    int32_t route;
};
#pragma pack(pop)

bool Schematic::SaveToFile(const std::wstring& path) const
{
    std::ofstream f(path.c_str(), std::ios::binary);
    if (!f) return false;

    FileHeader hdr{};
    std::memcpy(hdr.magic, "SCHEM", 5);
    hdr.version = 1;
    f.write(reinterpret_cast<char*>(&hdr), sizeof(hdr));

    int32_t nextId = m_nextId;
    f.write(reinterpret_cast<char*>(&nextId), sizeof(nextId));

    int32_t shapeCount = (int32_t)m_shapes.size();
    f.write(reinterpret_cast<char*>(&shapeCount), sizeof(shapeCount));

    for (const auto& s : m_shapes)
    {
        FileShape fs{};
        fs.id = s.Id;
        fs.type = (int32_t)s.Type;
        fs.x = s.Position.x; fs.y = s.Position.y;
        fs.angle = s.AngleDeg;
        fs.r = s.Color.r; fs.g = s.Color.g; fs.b = s.Color.b; fs.a = s.Color.a;
        fs.w = s.W; fs.h = s.H;
        fs.portCount = (int32_t)s.Ports.size();
        f.write(reinterpret_cast<char*>(&fs), sizeof(fs));

        for (const auto& p : s.Ports)
        {
            FilePort fp{ p.LocalPos.x, p.LocalPos.y, p.Normal.x, p.Normal.y };
            f.write(reinterpret_cast<char*>(&fp), sizeof(fp));
        }
    }

    int32_t connCount = (int32_t)m_connections.size();
    f.write(reinterpret_cast<char*>(&connCount), sizeof(connCount));

    for (const auto& c : m_connections)
    {
        FileConn fc{};
        fc.id = c.Id;
        fc.fromId = c.FromShapeId; fc.fromPort = c.FromPort;
        fc.toId = c.ToShapeId; fc.toPort = c.ToPort;
        fc.r = c.Color.r; fc.g = c.Color.g; fc.b = c.Color.b; fc.a = c.Color.a;
        fc.width = c.Width;
        fc.route = (int32_t)c.Route;
        f.write(reinterpret_cast<char*>(&fc), sizeof(fc));
    }

    return f.good();
}

bool Schematic::LoadFromFile(const std::wstring& path)
{
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f) return false;

    FileHeader hdr{};
    f.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    if (std::memcmp(hdr.magic, "SCHEM", 5) != 0) return false;

    f.read(reinterpret_cast<char*>(&m_nextId), sizeof(m_nextId));

    m_shapes.clear();
    m_connections.clear();
    m_shapeIndex.clear();

    int32_t shapeCount = 0;
    f.read(reinterpret_cast<char*>(&shapeCount), sizeof(shapeCount));

    for (int32_t i = 0; i < shapeCount; ++i)
    {
        FileShape fs{};
        f.read(reinterpret_cast<char*>(&fs), sizeof(fs));

        Shape s;
        s.Id = fs.id;
        s.Type = (ShapeType)fs.type;
        s.W = fs.w;
        s.H = fs.h;
        s.Position = P2F(fs.x, fs.y);
        s.AngleDeg = fs.angle;
        s.Color = ColorFRGB(fs.r, fs.g, fs.b, fs.a);

        if (s.Type == ShapeType::PowerSystem)
        {
            ComPtr<ID2D1RectangleGeometry> g;
            m_factory->CreateRectangleGeometry(
                D2D1::RectF(-fs.w / 2, -fs.h / 2, fs.w / 2, fs.h / 2), &g);
            s.Geometry = g;
        }
        else
        {
            ComPtr<ID2D1EllipseGeometry> g;
            m_factory->CreateEllipseGeometry(
                D2D1::Ellipse(D2D1::Point2F(0, 0), fs.w / 2, fs.h / 2), &g);
            s.Geometry = g;
        }

        s.Ports.resize(fs.portCount);
        for (int32_t p = 0; p < fs.portCount; ++p)
        {
            FilePort fp{};
            f.read(reinterpret_cast<char*>(&fp), sizeof(fp));
            s.Ports[p].LocalPos = P2F(fp.lx, fp.ly);
            s.Ports[p].Normal = P2F(fp.nx, fp.ny);
        }

        m_shapes.push_back(std::move(s));
    }

    int32_t connCount = 0;
    f.read(reinterpret_cast<char*>(&connCount), sizeof(connCount));

    for (int32_t i = 0; i < connCount; ++i)
    {
        FileConn fc{};
        f.read(reinterpret_cast<char*>(&fc), sizeof(fc));

        Connection c;
        c.Id = fc.id;
        c.FromShapeId = fc.fromId; c.FromPort = fc.fromPort;
        c.ToShapeId = fc.toId; c.ToPort = fc.toPort;
        c.Color = ColorFRGB(fc.r, fc.g, fc.b, fc.a);
        c.Width = fc.width;
        c.Route = (RouteKind)fc.route;
        c.Dirty = true;

        m_connections.push_back(std::move(c));
    }

    RebuildIndex();
    return f.good();
}

//*****************************************************
//*****************************************************
//*****************************************************





// ==================== Schematic ====================

Schematic::Schematic(ID2D1Factory* factory) : m_factory(factory) {}
Schematic::~Schematic() = default;

int Schematic::IndexOfShape(int id) const
{
    auto it = m_shapeIndex.find(id);
    return (it == m_shapeIndex.end()) ? -1 : it->second;
}

void Schematic::RebuildIndex()
{
    m_shapeIndex.clear();
    for (size_t i = 0; i < m_shapes.size(); ++i)
        m_shapeIndex[m_shapes[i].Id] = (int)i;
}

// ---------- Фигуры ----------


int Schematic::AddShape(ShapeType type, float w, float h, const D2D1_POINT_2F& pos, const D2D1_COLOR_F& color)
{
    Shape s;
    s.Id = m_nextId++;
    s.Type = type;
    s.W = w;
    s.H = h;
    s.Position = pos;
    s.Color = color;

    switch (type)
    {
    case ShapeType::PowerSystem:
    {
        ComPtr<ID2D1RectangleGeometry> g;
        m_factory->CreateRectangleGeometry(D2D1::RectF(-w / 2, -h / 2, w / 2, h / 2), &g);
        s.Geometry = g;
        s.Ports = MakePowerSystemPorts(w, h);
        break;
    }
    case ShapeType::Generator:
    {
        //ComPtr<ID2D1EllipseGeometry> g;
        //m_factory->CreateEllipseGeometry(D2D1::Ellipse(D2D1::Point2F(0, 0), w / 2, h / 2), &g);
        s.Geometry = BuildGeneratorGeometry(w);
        s.Ports = MakeGeneratorPorts(w / 2, h / 2);
        break;
    }

    case ShapeType::Transformer2w:
        s.Geometry = BuildTransformer2wGeometry(w, h);
        s.Ports = MakeTransformer2wPorts(w, h);
        break;

    case ShapeType::Bus:
        s.Geometry = BuildBusGeometry(w, h);
        s.Ports = MakeBusPorts(w, h, 5); // 5 отводов сверху/снизу
        break;
    }

    int idx = (int)m_shapes.size();
    m_shapes.push_back(std::move(s));
    m_shapeIndex[m_shapes[idx].Id] = idx;
    return idx;
}

void Schematic::DeleteShape(size_t index)
{
    if (index >= m_shapes.size()) return;
    const int id = m_shapes[index].Id;

    // Каскадное удаление проводов
    for (int i = (int)m_connections.size() - 1; i >= 0; --i)
    {
        auto& c = m_connections[i];
        if (c.FromShapeId == id || c.ToShapeId == id)
        {
            c.Path.Reset();
            m_connections.erase(m_connections.begin() + i);
        }
    }

    m_shapes[index].Geometry.Reset();
    m_shapes.erase(m_shapes.begin() + index);
    RebuildIndex();
}

void Schematic::MoveShape(size_t index, const D2D1_POINT_2F& newPos)
{
    if (index >= m_shapes.size()) return;
    m_shapes[index].Position = newPos;
    MarkWiresDirty(m_shapes[index].Id);
}

//--------------методы для подписей-----------------
ShapeType Schematic::GetShapeType(size_t i) const
{
    return (i < m_shapes.size()) ? m_shapes[i].Type : ShapeType::PowerSystem;
}

D2D1_MATRIX_3X2_F Schematic::GetShapeMatrix(size_t i) const
{
    return (i < m_shapes.size()) ? m_shapes[i].Matrix() : IdentityMatrix();
}


D2D1_POINT_2F Schematic::GetShapePosition(size_t i) const
{
    return (i < m_shapes.size()) ? m_shapes[i].Position : P2F(0, 0);
}

bool Schematic::IsShapeSelected(size_t i) const
{
    return (i < m_shapes.size()) && m_shapes[i].Selected;
}

int Schematic::FindShapeAt(const D2D1_POINT_2F& w) const
{
    for (int i = (int)m_shapes.size() - 1; i >= 0; --i) // сверху вниз
        if (m_shapes[i].HitTestWorld(w)) return i;
    return -1;
}

// ---------- Клеммы ----------

bool Schematic::FindPortAt(const D2D1_POINT_2F& w, float radius,
    int& shapeIdx, int& portIdx) const
{
    for (int i = (int)m_shapes.size() - 1; i >= 0; --i)
    {
        int p = m_shapes[i].NearestPort(w, radius);
        if (p >= 0) { shapeIdx = i; portIdx = p; return true; }
    }
    shapeIdx = portIdx = -1;
    return false;
}

D2D1_POINT_2F Schematic::GetPortWorld(int shapeIdx, int portIdx) const
{
    if (shapeIdx < 0 || shapeIdx >= (int)m_shapes.size()) return P2F(0, 0);
    return m_shapes[shapeIdx].PortWorld(portIdx);
}

D2D1_POINT_2F Schematic::GetPortNormalWorld(int shapeIdx, int portIdx) const
{
    if (shapeIdx < 0 || shapeIdx >= (int)m_shapes.size()) return P2F(1, 0);
    return m_shapes[shapeIdx].PortNormalWorld(portIdx);
}

std::vector<D2D1_POINT_2F> Schematic::GetPortPositions(int shapeIdx) const
{
    std::vector<D2D1_POINT_2F> out;
    if (shapeIdx < 0 || shapeIdx >= (int)m_shapes.size()) return out;
    const auto& s = m_shapes[shapeIdx];
    out.reserve(s.Ports.size());
    for (int i = 0; i < (int)s.Ports.size(); ++i)
        out.push_back(s.PortWorld(i));
    return out;
}

// ---------- Соединения ----------

int Schematic::AddConnection(int fromShapeIdx, int fromPort,
    int toShapeIdx, int toPort, RouteKind route)
{
    if (fromShapeIdx < 0 || fromShapeIdx >= (int)m_shapes.size()) return -1;
    if (toShapeIdx < 0 || toShapeIdx >= (int)m_shapes.size()) return -1;

    const int fromId = m_shapes[fromShapeIdx].Id;
    const int toId = m_shapes[toShapeIdx].Id;
    if (fromId == toId) return -1; // запрет петли на себя

    // Защита от дубликатов (в обе стороны)
    for (const auto& c : m_connections)
    {
        if ((c.FromShapeId == fromId && c.FromPort == fromPort &&
            c.ToShapeId == toId && c.ToPort == toPort) ||
            (c.FromShapeId == toId && c.FromPort == toPort &&
                c.ToShapeId == fromId && c.ToPort == fromPort))
            return -1;
    }

    Connection c;
    c.Id = m_nextId++;
    c.FromShapeId = fromId; c.FromPort = fromPort;
    c.ToShapeId = toId; c.ToPort = toPort;
    c.Color = ColorF(RGB(140, 90, 43)); // патинированная медь
    c.Width = 2.5f;
    c.Route = route;
    c.Dirty = true;
    c.Selected = false;

    int idx = (int)m_connections.size();
    m_connections.push_back(std::move(c));
    return idx;
}

bool Schematic::FindConnectionAt(const D2D1_POINT_2F& w, int& index) const
{
    for (int i = (int)m_connections.size() - 1; i >= 0; --i)
    {
        const auto& c = m_connections[i];
        if (!c.Path) continue;

        BOOL hit = FALSE;
        // Path хранится в МИРОВЫХ координатах -> worldTransform = nullptr
        if (SUCCEEDED(c.Path->StrokeContainsPoint(w, c.Width + 3.0f,
            nullptr, nullptr, 0.25f, &hit))
            && hit)
        {
            index = i;
            return true;
        }
    }
    index = -1;
    return false;
}

void Schematic::MarkWiresDirty(int shapeId)
{
    for (auto& c : m_connections)
        if (c.FromShapeId == shapeId || c.ToShapeId == shapeId)
            c.Dirty = true;
}

// ---------- Выделение ----------

void Schematic::SelectShape(int index)
{
    if (index >= 0 && index < (int)m_shapes.size())
        m_shapes[index].Selected = true;
}

void Schematic::ToggleSelectShape(int index)
{
    if (index >= 0 && index < (int)m_shapes.size())
        m_shapes[index].Selected = !m_shapes[index].Selected;
}

void Schematic::SelectConnection(int index)
{
    if (index >= 0 && index < (int)m_connections.size())
        m_connections[index].Selected = true;
}

void Schematic::ToggleSelectConnection(int index)
{
    if (index >= 0 && index < (int)m_connections.size())
        m_connections[index].Selected = !m_connections[index].Selected;
}

void Schematic::ClearSelection()
{
    for (auto& s : m_shapes) s.Selected = false;
    for (auto& c : m_connections) c.Selected = false;
}

void Schematic::DeleteSelectedShapes()
{
    std::unordered_set<int> ids;
    for (const auto& s : m_shapes)
        if (s.Selected) ids.insert(s.Id);
    if (ids.empty()) return;

    // 1. Каскад проводов
    for (int i = (int)m_connections.size() - 1; i >= 0; --i)
    {
        const auto& c = m_connections[i];
        if (ids.count(c.FromShapeId) || ids.count(c.ToShapeId))
            m_connections.erase(m_connections.begin() + i);
    }

    // 2. Сами фигуры (с конца)
    for (int i = (int)m_shapes.size() - 1; i >= 0; --i)
        if (m_shapes[i].Selected)
            m_shapes.erase(m_shapes.begin() + i);

    RebuildIndex();
}

void Schematic::DeleteSelectedConnections()
{
    for (int i = (int)m_connections.size() - 1; i >= 0; --i)
        if (m_connections[i].Selected)
            m_connections.erase(m_connections.begin() + i);
}

void Schematic::SelectShapesInRect(const D2D1_RECT_F& r)
{
    for (auto& s : m_shapes)
        if (RectsIntersect(s.WorldBounds(), r))
            s.Selected = true;
}

void Schematic::SelectConnectionsInRect(const D2D1_RECT_F& r)
{
    for (auto& c : m_connections)
        if (RectsIntersect(c.Bounds, r))
            c.Selected = true;
}

void Schematic::SelectAll()
{
    for (auto& s : m_shapes) s.Selected = true;
}

void Schematic::MoveSelectedShapes(const D2D1_POINT_2F& delta)
{
    for (auto& s : m_shapes)
        if (s.Selected)
        {
            s.Position = P2F(s.Position.x + delta.x, s.Position.y + delta.y);
            MarkWiresDirty(s.Id);
        }
}

int Schematic::SelectedShapeCount() const
{
    int n = 0;
    for (const auto& s : m_shapes) if (s.Selected) ++n;
    return n;
}

int Schematic::SelectedConnectionCount() const
{
    int n = 0;
    for (const auto& c : m_connections) if (c.Selected) ++n;
    return n;
}

bool Schematic::HasSelection() const
{
    return SelectedShapeCount() > 0 || SelectedConnectionCount() > 0;
}

// ---------- Построение геометрии проводов ----------

ComPtr<ID2D1PathGeometry> Schematic::BuildPolyline(
    const std::vector<D2D1_POINT_2F>& pts) const
{
    if (pts.size() < 2) return nullptr;

    ComPtr<ID2D1PathGeometry> path;
    if (FAILED(m_factory->CreatePathGeometry(&path))) return nullptr;

    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(path->Open(&sink))) return nullptr;

    sink->BeginFigure(pts[0], D2D1_FIGURE_BEGIN_HOLLOW);
    for (size_t i = 1; i < pts.size(); ++i)
        sink->AddLine(pts[i]);
    sink->EndFigure(D2D1_FIGURE_END_OPEN);

    if (FAILED(sink->Close())) return nullptr;
    return path;
}

ComPtr<ID2D1PathGeometry> Schematic::BuildBezier(
    const D2D1_POINT_2F& a, const D2D1_POINT_2F& an,
    const D2D1_POINT_2F& b, const D2D1_POINT_2F& bn) const
{
    const float dist = Distance(a, b);
    const float k = (std::max)(dist * 0.45f, 16.0f);

    D2D1_BEZIER_SEGMENT seg{};
    seg.point1 = P2F(a.x + an.x * k, a.y + an.y * k);
    seg.point2 = P2F(b.x + bn.x * k, b.y + bn.y * k);
    seg.point3 = b;

    ComPtr<ID2D1PathGeometry> path;
    if (FAILED(m_factory->CreatePathGeometry(&path))) return nullptr;

    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(path->Open(&sink))) return nullptr;

    sink->BeginFigure(a, D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddBezier(seg);
    sink->EndFigure(D2D1_FIGURE_END_OPEN);

    if (FAILED(sink->Close())) return nullptr;
    return path;
}

ComPtr<ID2D1PathGeometry> Schematic::BuildOrthogonal(
    const D2D1_POINT_2F& a, const D2D1_POINT_2F& an,
    const D2D1_POINT_2F& b, const D2D1_POINT_2F& bn, float stub) const
{
    const D2D1_POINT_2F a1 = P2F(a.x + an.x * stub, a.y + an.y * stub);
    const D2D1_POINT_2F b1 = P2F(b.x + bn.x * stub, b.y + bn.y * stub);

    const bool aHoriz = std::fabs(an.x) > std::fabs(an.y);
    const bool bHoriz = std::fabs(bn.x) > std::fabs(bn.y);

    std::vector<D2D1_POINT_2F> pts;

    if (aHoriz && bHoriz)
    {
        if (std::fabs(a1.y - b1.y) < 0.5f)
            pts = { a, a1, b1, b };
        else
        {
            const float midX = (a1.x + b1.x) * 0.5f;
            pts = { a, a1, P2F(midX, a1.y), P2F(midX, b1.y), b1, b };
        }
    }
    else if (!aHoriz && !bHoriz)
    {
        if (std::fabs(a1.x - b1.x) < 0.5f)
            pts = { a, a1, b1, b };
        else
        {
            const float midY = (a1.y + b1.y) * 0.5f;
            pts = { a, a1, P2F(a1.x, midY), P2F(b1.x, midY), b1, b };
        }
    }
    else
    {
        pts = aHoriz ? std::vector<D2D1_POINT_2F>{ a, a1, P2F(b1.x, a1.y), b1, b }
        : std::vector<D2D1_POINT_2F>{ a, a1, P2F(a1.x, b1.y), b1, b };
    }

    return BuildPolyline(pts);
}

ComPtr<ID2D1PathGeometry> Schematic::BuildWirePath(const Connection& c) const
{
    const int fi = IndexOfShape(c.FromShapeId);
    const int ti = IndexOfShape(c.ToShapeId);
    if (fi < 0 || ti < 0) return nullptr;

    const auto a = m_shapes[fi].PortWorld(c.FromPort);
    const auto b = m_shapes[ti].PortWorld(c.ToPort);
    const auto an = m_shapes[fi].PortNormalWorld(c.FromPort);
    const auto bn = m_shapes[ti].PortNormalWorld(c.ToPort);

    switch (c.Route)
    {
    case RouteKind::Straight: return BuildPolyline({ a, b });
    case RouteKind::Bezier: return BuildBezier(a, an, b, bn);
    case RouteKind::Orthogonal: return BuildOrthogonal(a, an, b, bn, 14.0f);
    }
    return nullptr;
}

void Schematic::RebuildWire(Connection& c)
{
    c.Path = BuildWirePath(c);
    if (c.Path)
    {
        D2D1_RECT_F b{};
        if (SUCCEEDED(c.Path->GetBounds(nullptr, &b)))
            c.Bounds = InflateRect(b, c.Width);
    }
    c.Dirty = false;
}

// ---------- Отрисовка ----------

void Schematic::DrawWire(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush,
    ID2D1StrokeStyle* stroke, size_t i)
{
    auto& c = m_connections[i];
    if (c.Dirty) RebuildWire(c);
    if (!c.Path) return;

    brush->SetColor(c.Color);
    rt->DrawGeometry(c.Path.Get(), brush, c.Width, stroke);
}

void Schematic::DrawWireSelected(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush,
    ID2D1StrokeStyle* stroke, size_t i)
{
    auto& c = m_connections[i];
    if (c.Dirty) RebuildWire(c);
    if (!c.Path) return;

    // Трёхслойное "янтарное свечение"
    brush->SetColor(ColorFRGB(1.0f, 0.69f, 0.0f, 0.25f));
    rt->DrawGeometry(c.Path.Get(), brush, c.Width + 8.0f, stroke);

    brush->SetColor(ColorFRGB(1.0f, 0.84f, 0.0f, 0.45f));
    rt->DrawGeometry(c.Path.Get(), brush, c.Width + 3.0f, stroke);

    brush->SetColor(ColorFRGB(1.0f, 0.77f, 0.36f, 1.0f));
    rt->DrawGeometry(c.Path.Get(), brush, c.Width, stroke);
}

void Schematic::DrawSelectionFrame(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush,
    ID2D1StrokeStyle* stroke, const Shape& s, float zoomHint)
{
    D2D1_RECT_F b = InflateRect(s.WorldBounds(), 6.0f);

    brush->SetColor(ColorFRGB(1.0f, 0.69f, 0.0f, 0.30f));
    rt->DrawRectangle(b, brush, 5.0f, stroke);

    brush->SetColor(ColorFRGB(1.0f, 0.84f, 0.0f, 0.90f));
    rt->DrawRectangle(b, brush, 1.5f, stroke);
}

void Schematic::Render(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush,
    ID2D1StrokeStyle* wireStroke, const D2D1_MATRIX_3X2_F& view)
{
    // 1. Обычные провода (мировые координаты -> только view)
    rt->SetTransform(view);
    for (size_t i = 0; i < m_connections.size(); ++i)
        if (!m_connections[i].Selected)
            DrawWire(rt, brush, wireStroke, i);

    // 2. Фигуры (локальные x view)
    for (const auto& s : m_shapes)
    {
        rt->SetTransform(MultiplyMatrix(s.Matrix(), view));

        brush->SetColor(s.Color);
        rt->FillGeometry(s.Geometry.Get(), brush);

        D2D1_COLOR_F edge = s.Color;
        edge.r *= 0.35f; edge.g *= 0.35f; edge.b *= 0.35f;
        brush->SetColor(edge);
        rt->DrawGeometry(s.Geometry.Get(), brush, 1.5f, wireStroke);
    }

    // 3. Рамки выделения
    rt->SetTransform(view);
    for (const auto& s : m_shapes)
        if (s.Selected) DrawSelectionFrame(rt, brush, wireStroke, s, 1.0f);

    // 4. Выделенные провода — поверх всего
    for (size_t i = 0; i < m_connections.size(); ++i)
        if (m_connections[i].Selected)
            DrawWireSelected(rt, brush, wireStroke, i);

    rt->SetTransform(view);

}

std::wstring Schematic::DebugInfo() const
{
    wchar_t buf[512];
    std::wstring out;
    swprintf_s(buf, L"Фигур: %zu, Соединений: %zu\n",
        m_shapes.size(), m_connections.size());
    out = buf;

    for (const auto& c : m_connections)
    {
        swprintf_s(buf, L" #%d: %d[%d] -> %d[%d]%s\n",
            c.Id, c.FromShapeId, c.FromPort, c.ToShapeId, c.ToPort,
            c.Selected ? L" [выделено]" : L"");
        out += buf;
    }
    return out;
}

//*******************************************************
//----------Геометрия компонентов----------------------
//********************************************************


//------------Генератор-----------------
ComPtr<ID2D1Geometry> Schematic::BuildGeneratorGeometry(float d) const
{
    ComPtr<ID2D1EllipseGeometry> circle1;
    m_factory->CreateEllipseGeometry(
        D2D1::Ellipse(D2D1::Point2F(0, 0), d / 2, d / 2), &circle1);

    // ── Вывод от круга к клемме ──
    ComPtr<ID2D1PathGeometry> leads;
    m_factory->CreatePathGeometry(&leads);

    ComPtr<ID2D1GeometrySink> sink;
    leads->Open(&sink);

    // 
    sink->BeginFigure(P2F(0, 35), D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddLine(P2F(0, 45));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);

    sink->Close();

    // ── Объединяем в одну геометрию ──
    ID2D1Geometry* parts[] = { circle1.Get(), leads.Get() };
    ComPtr<ID2D1GeometryGroup> group;

    m_factory->CreateGeometryGroup(D2D1_FILL_MODE_WINDING, parts, 2, &group);


    return group;
}

ComPtr<ID2D1Geometry> Schematic::BuildBusGeometry(float w, float h) const
{
    ComPtr<ID2D1RectangleGeometry> g;
    m_factory->CreateRectangleGeometry(
        D2D1::RectF(-w / 2, -h / 2, w / 2, h / 2), &g);
    return g;
}

// Вертикальная катушка из дуг (полуокружностей)
/*void Schematic::AddCoil(ID2D1GeometrySink* sink, float x, float yTop,
    float r, int n, bool bulgeRight) const
{
    for (int i = 0; i < n; ++i)
    {
        float y1 = yTop + (i + 1) * 2.0f * r;
        D2D1_ARC_SEGMENT arc{};
        arc.point = P2F(x, y1);
        arc.size = D2D1::SizeF(r, r);
        arc.rotationAngle = 0.0f;
        arc.sweepDirection = bulgeRight ? D2D1_SWEEP_DIRECTION_CLOCKWISE
            : D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE;
        arc.arcSize = D2D1_ARC_SIZE_SMALL;
        sink->AddArc(arc);
    }
}*/


// графические параметры 2-х обмоточного тр-ра
ComPtr<ID2D1Geometry> Schematic::BuildTransformer2wGeometry(float w, float h) const
{
    // ── Расположение центров кругов (совпадает с MakeTransformerPorts) ──
    const float x1 = -w / 2 + 18.0f; // центр первичной (левый)
    const float x2 = w / 2 - 18.0f; // центр вторичной (правый)
    const float d = x2 - x1; // расстояние между центрами

    // Радиус: достаточный для заметного пересечения, но с запасом на выводы
    // Увеличьте 0.6 → 0.75 для более сильного пересечения
    float r = (std::min)(d * 0.75f, h / 2 - 15.0f);
    if (r < 10.0f) r = 10.0f;

    // ── Два пересекающихся круга ──
    ComPtr<ID2D1EllipseGeometry> circle1, circle2;
    m_factory->CreateEllipseGeometry(D2D1::Ellipse(P2F(0, x1), r, r), &circle1);
    m_factory->CreateEllipseGeometry(D2D1::Ellipse(P2F(0, x2), r, r), &circle2);

    // ── Выводы от кругов к клеммам (верх и низ каждой обмотки) ──
    ComPtr<ID2D1PathGeometry> leads;
    m_factory->CreatePathGeometry(&leads);

    ComPtr<ID2D1GeometrySink> sink;
    leads->Open(&sink);

    // Первичная обмотка (верхний круг)
    sink->BeginFigure(P2F(0, -r - 27), D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddLine(P2F(0, -h + 17));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);

     /*sink->BeginFigure(P2F(x1, r), D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddLine(P2F(x1, h / 2));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);*/

    // Вторичная обмотка (нижний круг)
    sink->BeginFigure(P2F(0, r + 27), D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddLine(P2F(0, h - 17));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);

    /*sink->BeginFigure(P2F(x2, r), D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddLine(P2F(x2, h / 2));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);*/

    sink->Close();

    // ── Объединяем в одну геометрию ──
    ID2D1Geometry* parts[] = { circle1.Get(), circle2.Get(), leads.Get() };
    ComPtr<ID2D1GeometryGroup> group;
    m_factory->CreateGeometryGroup(D2D1_FILL_MODE_WINDING, parts, 3, &group);

    return group;
}