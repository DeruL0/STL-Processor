#pragma once

#include "MeshCore/MeshTypes.h"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

struct SwapS {
    std::int32_t tri0;
    std::int32_t op0;
    std::int32_t tri1;
    std::int32_t op1;
    std::int32_t iv0;
    std::int32_t iv1;
};

struct TempEdge {
    std::int32_t x;
    std::int32_t y;

    TempEdge() : x(0), y(0) {}
    TempEdge(const int& x_, const int& y_) : x(x_), y(y_) {}
    TempEdge(const TempEdge& other) {
        x = other.x;
        y = other.y;
    }

    TempEdge& operator=(const TempEdge& other) {
        x = other.x;
        y = other.y;
        return *this;
    }

    bool operator==(const TempEdge& other) const {
        return x == other.x && y == other.y;
    }
};

template<>
struct std::hash<TempEdge> {
    size_t operator()(const TempEdge& pointToHash) const {
        return static_cast<size_t>(pointToHash.x + 10 * pointToHash.y);
    }
};

struct HE_Vertex;
struct HE_Edge;
struct HE_Triangle;

enum METHOD {
    AREA,
    ANGLE
};

enum RMETHOD {
    NO_REFINEMENT,
    REFINEMENT
};

enum FMETHOD {
    NO_FAIRING,
    UNIFORM,
    SCALE,
    TAN,
    COT
};

enum DMETHOD {
    MEAN,
    MEDIAN,
    ALPHA,
    FUZZY,
    SUN
};

struct HE_Edge {
    HE_Vertex* Vertex = nullptr;
    HE_Vertex* VertexPre = nullptr;
    HE_Triangle* Triangle = nullptr;

    HE_Edge* Pair = nullptr;
    HE_Edge* Pre = nullptr;
    HE_Edge* Next = nullptr;

    bool MarkTwice = false;
    std::int8_t BoundaryType = 0;
};

struct HE_Vertex : Vertex {
    std::int32_t Index = 0;
    std::int32_t Step = 0;
    std::int32_t VisitCnt = 0;

    HE_Edge* Edge = nullptr;

    std::int8_t BoundaryType = 0;
    std::int8_t SurfaceType = 0;

    float Weight = 0.0f;
    std::int8_t GradientCnt = 0;

    bool operator==(const HE_Vertex& other) const {
        return Pos.x == other.Pos.x && Pos.y == other.Pos.y && Pos.z == other.Pos.z;
    }
};

template<>
struct std::hash<HE_Vertex> {
    size_t operator()(const HE_Vertex& v) const {
        return ((hash<float>()(v.Pos.x) ^ (hash<float>()(v.Pos.y) << 1)) >> 1) ^ (hash<float>()(v.Pos.z) << 1);
    }
};

struct HE_Triangle : Triangle {
    HE_Edge* Edge = nullptr;
    bool IsMarked = false;
};

class HE_MeshData {
public:
    HE_MeshData() = default;
    ~HE_MeshData();

    std::vector<HE_Vertex*> HE_Vertexes;
    std::map<std::pair<std::int32_t, std::int32_t>, HE_Edge*> HE_Edges;
    std::vector<HE_Triangle*> HE_Triangles;
    std::vector<HE_Triangle*> New_HE_Triangles;
    std::vector<std::int32_t> Indices32;

    void ReadSTL(std::string file_path, std::string file_name);
    void ExportVTK(std::string file_path, std::string file_name);
    void ExportSTL(std::string file_path, std::string file_name);

    void GetSteps(std::int32_t cnt, std::int32_t index);

    void RepairNormal();

    void Denoise(int denoiseNum, float T);

    void HolesFilling(METHOD method, RMETHOD refinement, FMETHOD fairing, int fairingNum);

    void Smoothing(int fairingNum, FMETHOD fairing);

    void GetVertexesNormal();

private:
    void Clear();
    void ClearTopology();

    HE_Triangle* CreateHE_Triangle(std::uint32_t index, Vec3f normal, std::vector<std::int32_t>& VertexIndices);
    HE_Edge* CreateHE_Edge(std::int32_t start, std::int32_t end);

    std::vector<HE_Vertex*> GetVertexsFromTriangle(const HE_Triangle* triangle);
    std::vector<HE_Vertex*> GetVertexesFromVertex(const HE_Vertex* vertex);

    std::vector<HE_Edge*> GetEdgesFromVertex(const HE_Vertex* vertex);
    std::vector<HE_Edge*> GetEdgesFromEdge(const HE_Edge* edge);

    std::vector<HE_Triangle*> GetTrianglesFromTriangle(const HE_Triangle* triangle);
    std::vector<HE_Triangle*> GetTrianglesFromVertex(const HE_Vertex* vertex);

    void GetVertexNormal(HE_Vertex* vertex);

    int GetIntersection(HE_Triangle* facet);
    void CorrectTriangle(HE_Triangle* wrongTriangle);
    void CorrectNormals(HE_Triangle* correctTriangle);
    void CorrectTrianglesFromTriangle(HE_Triangle* triangle, std::queue<HE_Triangle*>& qfacets);

    void DenoiseHelper(float T);

    std::vector<std::vector<std::int32_t>> BoundaryLoops;

    void HolesIdentification();
    void HolesFillingHelper(
        std::vector<std::int32_t>& boundaryLoop,
        METHOD method,
        RMETHOD refinement,
        FMETHOD fairing,
        int fairingNum
    );

    void Refinement(
        std::vector<std::int32_t>& boundaryLoop,
        std::vector<std::vector<std::int32_t>>& ls,
        std::vector<std::vector<std::int32_t>>& triangles,
        RMETHOD refinement
    );

    void TrianglesToBeInserted(
        std::vector<std::int32_t>& boundaryLoop,
        std::vector<std::vector<std::int32_t>>& ls,
        std::vector<Vec3f>& centroids,
        std::vector<std::vector<std::int32_t>>& triangles
    );
    void CalculateSigmas(
        std::vector<std::int32_t>& boundaryLoop,
        std::unordered_map<std::int32_t, float>& sigmas
    );
    void FetchCentroidSigmas(
        std::vector<std::vector<std::int32_t>>& triangles,
        std::vector<float>& centroidSigmas,
        std::unordered_map<std::int32_t, float>& sigmas
    );
    void IdentifyDividedTriangles(
        std::vector<std::vector<std::int32_t>>& triangles,
        std::vector<Vec3f>& centroids,
        std::unordered_map<std::int32_t, float>& sigmas,
        std::vector<float>& centroidSigmas,
        std::vector<std::int32_t>& trianglesToBeDivided
    );
    void DivideTriangles(
        std::vector<std::vector<std::int32_t>>& triangles,
        std::vector<std::int32_t>& trianglesToBeDivided,
        std::vector<Vec3f>& centroids,
        std::unordered_map<std::int32_t, float>& sigmas,
        std::vector<float>& centroidSigmas,
        std::vector<std::pair<std::int32_t, std::int32_t>>& edgesToBeRelaxed
    );

    void RelaxEdge(
        std::vector<std::vector<std::int32_t>>& triangles,
        std::int32_t v0, std::int32_t v1,
        std::vector<Vec3f>& centroids,
        std::unordered_map<std::int32_t, float>& sigmas,
        std::vector<float>& centroidSigmas
    );
    bool CircumSphereCheck(
        std::int32_t v0_, std::int32_t v1_,
        std::int32_t v2_, std::int32_t op_
    );
    bool SwapCheck(
        const std::vector<std::vector<std::int32_t>>& triangles,
        std::int32_t iv0, std::int32_t iv1,
        std::int32_t op0, std::int32_t op1
    );
    void Relax(
        std::vector<std::vector<std::int32_t>>& triangles,
        std::int32_t& numOfSwap, std::int32_t& sameCounter,
        std::vector<Vec3f>& centroids,
        std::unordered_map<std::int32_t, float>& sigmas,
        std::vector<float>& centroidSigmas
    );
    bool EdgeCheck(
        std::vector<std::vector<std::int32_t>>& triangles,
        std::unordered_map<TempEdge, std::int32_t>& edgesToTriangles,
        std::int32_t v0, std::int32_t v1, std::int32_t v2, std::int32_t i,
        std::unordered_set<std::int32_t>& swapSet,
        std::vector<SwapS>& toBeSwapped
    );
    void Swap(
        std::vector<std::vector<std::int32_t>>& triangles,
        std::int32_t tri0, std::int32_t op0,
        std::int32_t tri1, std::int32_t op1,
        std::int32_t iv0, std::int32_t iv1,
        std::vector<Vec3f>& centroids,
        std::unordered_map<std::int32_t, float>& sigmas,
        std::vector<float>& centroidSigmas
    );

    void InsertTriangles(std::vector<std::vector<std::int32_t>>& triangles);

    void Fairing(enum FMETHOD fairing);
    float GetWeight(HE_Vertex* vertex, HE_Vertex* neighbor, enum FMETHOD fairing);
    float GetWeightHarmonic(HE_Vertex* vertex, HE_Vertex* neighbor, enum FMETHOD fairing);

    void ReconstructTopology();
};
