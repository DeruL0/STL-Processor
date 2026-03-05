#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

struct Vec2f {
    float x = 0.0f;
    float y = 0.0f;

    Vec2f() = default;
    Vec2f(float x_, float y_) : x(x_), y(y_) {}
};

struct Vec3f {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    Vec3f() = default;
    Vec3f(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
};

struct Triangle {
    std::uint32_t Index = 0;
    Vec3f Normal;

    std::int32_t VertexIndex0 = 0;
    std::int32_t VertexIndex1 = 0;
    std::int32_t VertexIndex2 = 0;
};

struct Vertex {
    Vec3f Pos;
    Vec3f Normal;
    Vec2f TexC;

    bool operator==(const Vertex& other) const {
        return Pos.x == other.Pos.x && Pos.y == other.Pos.y && Pos.z == other.Pos.z;
    }
};

template<>
struct std::hash<Vertex> {
    size_t operator()(const Vertex& v) const {
        return ((hash<float>()(v.Pos.x) ^ (hash<float>()(v.Pos.y) << 1)) >> 1) ^ (hash<float>()(v.Pos.z) << 1);
    }
};
