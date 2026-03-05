#include "MeshCore/HalfEdgeMesh.h"
#include "MeshMath.h"

#include <unordered_set>

HE_MeshData::~HE_MeshData() {
    Clear();
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

	edges.push_back(start);
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
	HE_Edge* edge = triangle->Edge;
	HE_Edge* temp = edge;
	std::vector<HE_Triangle*> facets;

	do {
		facets.push_back(temp->Pair->Triangle);
		temp = temp->Next;
	} while (temp != edge);

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
