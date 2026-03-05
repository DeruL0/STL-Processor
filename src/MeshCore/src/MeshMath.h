#pragma once

#include "MeshCore/MeshTypes.h"

#include <algorithm>
#include <cmath>

inline Vec3f SubVector(const Vec3f& v0, const Vec3f& v1) {
    return Vec3f(v0.x - v1.x, v0.y - v1.y, v0.z - v1.z);
}

inline Vec3f CrossVector(const Vec3f& v0, const Vec3f& v1) {
    return Vec3f(
        v0.y * v1.z - v0.z * v1.y,
        v0.z * v1.x - v0.x * v1.z,
        v0.x * v1.y - v0.y * v1.x
    );
}

inline float DotVector(const Vec3f& v0, const Vec3f& v1) {
    return v0.x * v1.x + v0.y * v1.y + v0.z * v1.z;
}

inline float Length(const Vec3f& v) {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

inline Vec3f Normalize(const Vec3f& v) {
    const float length = Length(v);
    if (length <= 0.0f) {
        return Vec3f(0.0f, 0.0f, 0.0f);
    }
    const float invLength = 1.0f / length;
    return Vec3f(v.x * invLength, v.y * invLength, v.z * invLength);
}

inline Vec3f CalTriNormal(const Vec3f& v0, const Vec3f& v1, const Vec3f& v2) {
    const Vec3f u = SubVector(v1, v0);
    const Vec3f v = SubVector(v2, v1);
    return Normalize(CrossVector(u, v));
}

inline float GetTriArea(const Vec3f& v0, const Vec3f& v1, const Vec3f& v2) {
    const Vec3f v0v1 = SubVector(v1, v0);
    const Vec3f v0v2 = SubVector(v2, v0);
    const Vec3f cross = CrossVector(v0v1, v0v2);
    return 0.5f * std::sqrt(cross.x * cross.x + cross.y * cross.y + cross.z * cross.z);
}

inline float AngleBetweenVectors(const Vec3f& v0, const Vec3f& v1) {
    const float denom = Length(v0) * Length(v1);
    if (denom <= 0.0f) {
        return 0.0f;
    }
    float cosine = DotVector(v0, v1) / denom;
    cosine = std::max(-1.0f, std::min(1.0f, cosine));
    return std::acos(cosine);
}
