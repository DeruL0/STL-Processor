#include "MeshCore/HalfEdgeMesh.h"
#include "MeshMath.h"

#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

HE_MeshData::~HE_MeshData() {
    Clear();
}

HE_MeshData::HE_MeshData(HE_MeshData&& other) noexcept {
    *this = std::move(other);
}

HE_MeshData& HE_MeshData::operator=(HE_MeshData&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    Clear();

    HE_Vertexes = std::move(other.HE_Vertexes);
    HE_Edges = std::move(other.HE_Edges);
    HE_Triangles = std::move(other.HE_Triangles);
    New_HE_Triangles = std::move(other.New_HE_Triangles);
    Indices32 = std::move(other.Indices32);
    BoundaryLoops = std::move(other.BoundaryLoops);

    other.HE_Vertexes.clear();
    other.HE_Edges.clear();
    other.HE_Triangles.clear();
    other.New_HE_Triangles.clear();
    other.Indices32.clear();
    other.BoundaryLoops.clear();
    return *this;
}

void HE_MeshData::Clear() {
    std::unordered_set<HE_Edge*> edges;
    for (const auto& kv : HE_Edges) {
        edges.insert(kv.second);
    }
    for (HE_Edge* edge : edges) {
        delete edge;
    }

    for (HE_Triangle* triangle : HE_Triangles) {
        delete triangle;
    }

    for (HE_Vertex* vertex : HE_Vertexes) {
        delete vertex;
    }

    HE_Edges.clear();
    HE_Triangles.clear();
    New_HE_Triangles.clear();
    HE_Vertexes.clear();
    Indices32.clear();
    BoundaryLoops.clear();
}

void HE_MeshData::ClearTopology() {
    std::unordered_set<HE_Edge*> edges;
    for (const auto& kv : HE_Edges) {
        edges.insert(kv.second);
    }
    for (HE_Edge* edge : edges) {
        delete edge;
    }

    for (HE_Triangle* triangle : HE_Triangles) {
        delete triangle;
    }

    HE_Edges.clear();
    HE_Triangles.clear();
    New_HE_Triangles.clear();
}

void HE_MeshData::SetIndexedMesh(
    const std::vector<Vec3f>& positions,
    const std::vector<std::array<std::int32_t, 3>>& triangles
) {
    Clear();

    HE_Vertexes.reserve(positions.size());
    for (std::size_t i = 0; i < positions.size(); ++i) {
        HE_Vertex* vertex = new HE_Vertex();
        vertex->Index = static_cast<std::int32_t>(i);
        vertex->Pos = positions[i];
        HE_Vertexes.push_back(vertex);
    }

    HE_Triangles.reserve(triangles.size());
    for (std::size_t i = 0; i < triangles.size(); ++i) {
        const std::array<std::int32_t, 3>& tri = triangles[i];
        if (tri[0] < 0 || tri[1] < 0 || tri[2] < 0 ||
            static_cast<std::size_t>(tri[0]) >= positions.size() ||
            static_cast<std::size_t>(tri[1]) >= positions.size() ||
            static_cast<std::size_t>(tri[2]) >= positions.size()) {
            std::ostringstream oss;
            oss << "Triangle " << i << " has an out-of-range vertex index.";
            throw std::runtime_error(oss.str());
        }
        if (tri[0] == tri[1] || tri[1] == tri[2] || tri[2] == tri[0]) {
            std::ostringstream oss;
            oss << "Triangle " << i << " is degenerate.";
            throw std::runtime_error(oss.str());
        }

        std::vector<std::int32_t> vertexIndices = { tri[0], tri[1], tri[2] };
        const Vec3f normal = CalTriNormal(
            positions[tri[0]],
            positions[tri[1]],
            positions[tri[2]]
        );
        HE_Triangles.push_back(CreateHE_Triangle(static_cast<std::uint32_t>(i), normal, vertexIndices));
    }

    GetVertexesNormal();
}

bool HE_MeshData::IsClosedTwoManifold(std::string* error) const {
    if (HE_Triangles.empty() || HE_Vertexes.empty()) {
        if (error != nullptr) {
            *error = "Mesh is empty.";
        }
        return false;
    }

    struct EdgeInfo {
        int Count = 0;
    };

    std::unordered_map<std::uint64_t, EdgeInfo> edgeCounts;
    edgeCounts.reserve(HE_Triangles.size() * 3);

    auto encodeEdge = [](std::int32_t a, std::int32_t b) -> std::uint64_t {
        const std::uint32_t lo = static_cast<std::uint32_t>(std::min(a, b));
        const std::uint32_t hi = static_cast<std::uint32_t>(std::max(a, b));
        return (static_cast<std::uint64_t>(lo) << 32u) | static_cast<std::uint64_t>(hi);
    };

    for (std::size_t triIndex = 0; triIndex < HE_Triangles.size(); ++triIndex) {
        const HE_Triangle* triangle = HE_Triangles[triIndex];
        if (triangle == nullptr) {
            if (error != nullptr) {
                *error = "Mesh contains a null triangle.";
            }
            return false;
        }

        const std::array<std::int32_t, 3> indices = {
            triangle->VertexIndex0,
            triangle->VertexIndex1,
            triangle->VertexIndex2
        };

        if (indices[0] < 0 || indices[1] < 0 || indices[2] < 0 ||
            static_cast<std::size_t>(indices[0]) >= HE_Vertexes.size() ||
            static_cast<std::size_t>(indices[1]) >= HE_Vertexes.size() ||
            static_cast<std::size_t>(indices[2]) >= HE_Vertexes.size()) {
            if (error != nullptr) {
                std::ostringstream oss;
                oss << "Triangle " << triIndex << " has an invalid vertex index.";
                *error = oss.str();
            }
            return false;
        }

        if (indices[0] == indices[1] || indices[1] == indices[2] || indices[2] == indices[0]) {
            if (error != nullptr) {
                std::ostringstream oss;
                oss << "Triangle " << triIndex << " is degenerate.";
                *error = oss.str();
            }
            return false;
        }

        const Vec3f& v0 = HE_Vertexes[indices[0]]->Pos;
        const Vec3f& v1 = HE_Vertexes[indices[1]]->Pos;
        const Vec3f& v2 = HE_Vertexes[indices[2]]->Pos;
        if (GetTriArea(v0, v1, v2) <= 1e-8f) {
            if (error != nullptr) {
                std::ostringstream oss;
                oss << "Triangle " << triIndex << " has near-zero area.";
                *error = oss.str();
            }
            return false;
        }

        edgeCounts[encodeEdge(indices[0], indices[1])].Count++;
        edgeCounts[encodeEdge(indices[1], indices[2])].Count++;
        edgeCounts[encodeEdge(indices[2], indices[0])].Count++;
    }

    for (const auto& [edgeKey, info] : edgeCounts) {
        if (info.Count != 2) {
            if (error != nullptr) {
                const std::uint32_t v0 = static_cast<std::uint32_t>(edgeKey >> 32u);
                const std::uint32_t v1 = static_cast<std::uint32_t>(edgeKey & 0xffffffffu);
                std::ostringstream oss;
                if (info.Count < 2) {
                    oss << "Mesh has a boundary edge (" << v0 << ", " << v1 << ").";
                } else {
                    oss << "Mesh has a non-manifold edge (" << v0 << ", " << v1 << ").";
                }
                *error = oss.str();
            }
            return false;
        }
    }

    return true;
}

HE_Triangle* HE_MeshData::CreateHE_Triangle(std::uint32_t index, Vec3f normal, std::vector<std::int32_t>& VertexIndices) {
	HE_Triangle* he_triangle = new HE_Triangle();

	he_triangle->Index = index;
	he_triangle->Normal = normal;
	he_triangle->VertexIndex0 = VertexIndices[0];
	he_triangle->VertexIndex1 = VertexIndices[1];
	he_triangle->VertexIndex2 = VertexIndices[2];

	HE_Edge* he_edges[3];

	//create edges of facet
	for (int i = 0; i < 3; i++) {
		std::int32_t u = VertexIndices[i % 3];
		std::int32_t v = VertexIndices[(i + 1) % 3];
		he_edges[i] = CreateHE_Edge(u, v);
	}

	//create topo of facet
	for (int i = 0; i < 3; i++) {
		he_edges[i]->Next = he_edges[(i + 1) % 3];
		he_edges[i]->Pre = he_edges[(i + 2) % 3];
		he_edges[i]->Triangle = he_triangle;
	}

	he_triangle->Edge = he_edges[0];

	return he_triangle;
}

HE_Edge* HE_MeshData::CreateHE_Edge(std::int32_t start, std::int32_t end) {
	//sameEdge*2 && ×next → wrongEdge, facet_other = 1st facet
	if (HE_Edges.find(std::make_pair(start, end)) != HE_Edges.end()) {
		if (HE_Edges.at(std::make_pair(start, end))->Pair->Next == nullptr) {
			HE_Edges.at(std::make_pair(start, end))->MarkTwice = true;
			HE_Edges.at(std::make_pair(start, end))->Pair->Triangle = HE_Edges.at(std::make_pair(start, end))->Triangle;
		}
		return HE_Edges[std::make_pair(start, end)];
	}

	HE_Edge* he_edge = new HE_Edge();
	HE_Edge* he_pair = new HE_Edge();

	he_edge->Vertex = HE_Vertexes[end];
	he_edge->VertexPre = HE_Vertexes[start];
	he_edge->Pair = he_pair;

	he_pair->Vertex = HE_Vertexes[start];
	he_pair->VertexPre = HE_Vertexes[end];
	he_pair->Pair = he_edge;

	HE_Vertexes[start]->Edge = he_edge;
	HE_Vertexes[end]->Edge = he_pair;

	HE_Edges[std::make_pair(start, end)] = he_edge;
	HE_Edges[std::make_pair(end, start)] = he_pair;

	return he_edge;
}


std::vector<HE_Vertex*> HE_MeshData::GetVertexsFromTriangle(const HE_Triangle* triangle) {
	std::vector<HE_Vertex*> vertexes;

	HE_Edge* edge = triangle->Edge;
	 
	do {
		vertexes.push_back(edge->Vertex);
		edge = edge->Next;
	} while (edge != triangle->Edge);

	return vertexes;
}

std::vector<HE_Vertex*> HE_MeshData::GetVertexesFromVertex(const HE_Vertex* vertex) {
	std::vector<HE_Edge*> edges = GetEdgesFromVertex(vertex);
	std::vector<HE_Vertex*> vertexes;

	for (std::uint32_t i = 0; i < edges.size(); i++) {
		vertexes.push_back(edges[i]->Vertex);
	}

	return vertexes;
}


std::vector<HE_Edge*> HE_MeshData::GetEdgesFromVertex(const HE_Vertex* vertex) {
	std::vector<HE_Edge*> edges;
	HE_Edge* start = vertex->Edge;
	if (start == nullptr) {
		return edges;
	}
	HE_Edge* temp = start;

	//rule 1(!boundary): edge->pair->next
	do {
		temp = temp->Pair;
		if (temp->Next == nullptr) {
			break;
		}
		temp = temp->Next;
		edges.push_back(temp);
	} while (temp != start);

	//rule 2(boundary): edge->pre->pair
	if (temp->Triangle == nullptr) {
		temp = temp->Pair->Pre;

		do {
			temp = temp->Pair;
			if (temp->Triangle == nullptr) {
				break;
			}
			edges.push_back(temp);
			temp = temp->Pre;
		} while (temp != start);
	}

	if (edges.empty() || edges.back() != start) {
		edges.push_back(start);
	}
	return edges;
}

std::vector<HE_Edge*> HE_MeshData::GetEdgesFromEdge(const HE_Edge* edge) {
	std::vector<HE_Edge*> next_edges = GetEdgesFromVertex(edge->Next->Vertex);
	std::vector<HE_Edge*> pair_edges = GetEdgesFromVertex(edge->Vertex);

	for (int i = 0; i < pair_edges.size(); i++) {
		next_edges.push_back(pair_edges[i]->Pair);
	}
	return next_edges;
}


std::vector<HE_Triangle*> HE_MeshData::GetTrianglesFromTriangle(const HE_Triangle* triangle) {
	std::vector<HE_Triangle*> facets;
	if (triangle == nullptr || triangle->Edge == nullptr) {
		return facets;
	}

	HE_Edge* edge = triangle->Edge;
	HE_Edge* temp = edge;

	do {
		if (temp == nullptr || temp->Pair == nullptr) {
			break;
		}
		if (temp->Pair->Triangle != nullptr) {
			facets.push_back(temp->Pair->Triangle);
		}
		temp = temp->Next;
	} while (temp != nullptr && temp != edge);

	return facets;
}

std::vector<HE_Triangle*> HE_MeshData::GetTrianglesFromVertex(const HE_Vertex* vertex) {
	std::vector<HE_Edge*> edges = GetEdgesFromVertex(vertex);
	std::vector<HE_Triangle*> triangles;
	triangles.reserve(edges.size());

	for (std::int32_t i = 0; i < edges.size(); i++) {
		HE_Edge* edge = edges[i];
		if (edge == nullptr || edge->Triangle == nullptr) {
			continue;
		}
		triangles.push_back(edge->Triangle);
	}

	return triangles;
}


void HE_MeshData::GetSteps(std::int32_t cnt, std::int32_t index) {
	std::vector<HE_Vertex*> vertexes = GetVertexesFromVertex(HE_Vertexes[index]);
	std::queue<HE_Vertex*> qvertexes;

	//initialize
	for (int i = 0; i < vertexes.size(); i++) {
		qvertexes.push(vertexes[i]);
		vertexes[i]->Step = 0;
		vertexes[i]->VisitCnt++;
	}

	//empty points
	for (std::vector<HE_Vertex*>::iterator it = vertexes.begin(); it != vertexes.end(); ++it) {
		if (*it != nullptr) {
			//delete[](*it);
			*it = nullptr;
		}
	}
	vertexes.clear();
	vertexes.shrink_to_fit();

	//BFS(priority queue)
	while (!qvertexes.empty()) {
		HE_Vertex* temp = qvertexes.front();
		qvertexes.pop();
		vertexes = GetVertexesFromVertex(temp);

		for (int i = 0; i < vertexes.size(); i++) {
			//!visit this time
			if (vertexes[i]->VisitCnt == cnt) {
				qvertexes.push(vertexes[i]);
				//1st GetSteps()
				if (cnt == 0) {
					vertexes[i]->Step = temp->Step + 1;
				}
				//did GetSteps() before
				else {
					vertexes[i]->Step = (temp->Step + 1 < vertexes[i]->Step ? temp->Step + 1 : vertexes[i]->Step);
				}
				vertexes[i]->VisitCnt++;
			}
		}

		for (std::vector<HE_Vertex*>::iterator it = vertexes.begin(); it != vertexes.end(); ++it) {
			if (*it != nullptr) {
				//delete[](*it);
				*it = nullptr;
			}
		}
		vertexes.clear();
		vertexes.shrink_to_fit();
	}
}

void HE_MeshData::ReconstructTopology() {
	std::vector<std::vector<std::int32_t>> indices;
	for (std::uint32_t i = 0; i < HE_Triangles.size(); i++){
		std::vector<std::int32_t> temp;
		temp.push_back(HE_Triangles[i]->VertexIndex2);
		temp.push_back(HE_Triangles[i]->VertexIndex1);
		temp.push_back(HE_Triangles[i]->VertexIndex0);
		indices.push_back(temp);
	}
	ClearTopology();
	InsertTriangles(indices);
}



//Get Vertexes Normal==========================================================================================================================
void HE_MeshData::GetVertexesNormal() {
	for (std::int32_t i = 0; i < HE_Vertexes.size(); i++){
		if (HE_Vertexes[i] == nullptr) {
			continue;
		}
		GetVertexNormal(HE_Vertexes[i]);
	}
}

void HE_MeshData::GetVertexNormal(HE_Vertex* vertex) {
	std::vector<HE_Triangle*> triangles = GetTrianglesFromVertex(vertex);
	Vec3f normal = Vec3f(0.0f, 0.0f, 0.0f);

	for (int i = 0; i < triangles.size(); i++){
		if (triangles[i] == nullptr) {
			continue;
		}
		normal.x += triangles[i]->Normal.x;
		normal.y += triangles[i]->Normal.y;
		normal.z += triangles[i]->Normal.z;
	}

	normal = Normalize(normal);
	vertex->Normal = normal;
}
