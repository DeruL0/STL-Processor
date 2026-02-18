#include "HalfEdgeStructure.h"


//File Operations==========================================================================================================================
void HE_MeshData::ReadSTL(std::string file_path, std::string file_name) {
	file_path.append(file_name);
	file_path.append(".stl");
	
	std::ifstream fin(file_path);

	if (!fin) {
		MessageBox(0, L"Model not found.", 0, 0);
		return;
	}

	std::unordered_map<HE_Vertex, std::int32_t> occ_vertexes;
	std::int32_t vcount = 0;
	std::int32_t tcount = 0;
	std::string ignore;
	float x, y, z;

	//skip "solid name"
	fin >> ignore >> ignore;

	do {
		//skip "facet normal x y z" & detect end
		fin >> ignore >> ignore >> x >> y >> z;
		if (ignore != "normal") {
			break;
		}

		//skip "outer loop"
		fin >> ignore >> ignore;

		//skip "vertex" & get x,y,z
		std::vector<std::int32_t> vertex_indices;

		for (int i = 0; i < 3; i++) {
			fin >> ignore >> x >> y >> z;

			HE_Vertex tempVertex;
			tempVertex.Pos = DirectX::XMFLOAT3(x, y, z);

			if (occ_vertexes.count(tempVertex) == 0) {
				occ_vertexes.insert(std::make_pair(tempVertex, vcount));

				HE_Vertex* temp = new HE_Vertex();
				temp->Index = vcount++;
				temp->Pos = DirectX::XMFLOAT3(x, y, z);
				temp->Normal = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);

				HE_Vertexes.push_back(temp);
			}

			vertex_indices.push_back(occ_vertexes.at(tempVertex));
		}

		//get correct normal
		DirectX::XMFLOAT3 correctNormal = MathHelper::CalTriNormal(
			HE_Vertexes[vertex_indices[0]]->Pos,
			HE_Vertexes[vertex_indices[1]]->Pos,
			HE_Vertexes[vertex_indices[2]]->Pos
		);

		//create facet
		HE_Triangles.push_back(
			CreateHE_Triangle(tcount++, correctNormal, vertex_indices)
		);

		//skip "endloop endfacet"
		fin >> ignore >> ignore;
	} while (1);

	fin.close();
}

void HE_MeshData::ExportVTK(std::string file_path, std::string file_name) {
	//open file and write
	file_path.append(file_name);
	file_path.append(".vtk");

	std::fstream file;
	file.open(file_path, std::ios::out);

	//Header
	file << "# vtk DataFile Version 2.0" << std::endl;
	file << file_name << std::endl;
	file << "ASCII" << std::endl;
	file << "DATASET POLYDATA" << std::endl;

	//Points
	std::int32_t points_size = HE_Vertexes.size();
	file << "Points " << points_size << " float" << std::endl;

	for (std::int32_t i = 0; i < points_size; i++) {
		file << HE_Vertexes[i]->Pos.x << ' ' << HE_Vertexes[i]->Pos.y << ' ' << HE_Vertexes[i]->Pos.z << std::endl;
	}

	//POLYGONS
	file << "POLYGONS " << HE_Triangles.size() << ' ' << HE_Triangles.size() * 4 << std::endl;

	for (UINT i = 0; i < HE_Triangles.size(); i++) {
		file << 3 << ' ' << HE_Triangles[i]->VertexIndex0 << ' ' << HE_Triangles[i]->VertexIndex1 << ' ' << HE_Triangles[i]->VertexIndex2 << std::endl;
	}

	//CELLDATA
	file << "CELL_DATA " << HE_Triangles.size() << std::endl;
	file << "SCALARS cell_scalars float 1  " << std::endl;
	file << "LOOKUP_TABLE default  " << std::endl;

	for (UINT i = 0; i < HE_Triangles.size(); i++) {
		file << i << std::endl;
	}

	//NORMALS
	file << "NORMALS cell_normals float" << std::endl;

	for (UINT i = 0; i < HE_Triangles.size(); i++) {
		file << HE_Triangles[i]->Normal.x << ' ' << HE_Triangles[i]->Normal.y << ' ' << HE_Triangles[i]->Normal.z << std::endl;
	}

	//Cellids
	file << "FIELD FieldData 2 " << std::endl;
	file << "cellIds 1 " << HE_Triangles.size() << " int " << std::endl;

	for (UINT i = 0; i < HE_Triangles.size(); i++) {
		file << i << std::endl;
	}

	//FaceAttributes
	file << "faceAttributes 1 " << HE_Triangles.size() << " float " << std::endl;
	for (int i = 0; i < HE_Triangles.size(); i++) {
		file << i << std::endl;
	}

	//PointData
	file << "POINT_DATA " << HE_Vertexes.size() << std::endl;
	file << "SCALARS sample_scalars float 1 " << std::endl;
	file << "LOOKUP_TABLE default" << std::endl;

	for (int i = 0; i < HE_Vertexes.size(); i++) {
		file << HE_Vertexes[i]->Step << std::endl;
	}

	//	if (LoadedHE_Points[i]->boundaryType >= 0 ) {
	//		file << 1 << endl;
	//	}
	//	else {
	//		file << 0 << endl;
	//	}

	//	/*if		(LoadedHE_Points[i]->normalExpect.x() > 0.0f)		file << 0 << endl;
	//	else if (LoadedHE_Points[i]->normalExpect.x() < 0.0f)		file << 1 << endl;
	//	else if (LoadedHE_Points[i]->normalExpect.y() > 0.0f)		file << 2 << endl;
	//	else if (LoadedHE_Points[i]->normalExpect.y() < 0.0f)		file << 3 << endl;
	//	else if (LoadedHE_Points[i]->normalExpect.z() > 0.0f)		file << 4 << endl;
	//	else if (LoadedHE_Points[i]->normalExpect.z() < 0.0f) 		file << 5 << endl;*/

}

void HE_MeshData::ExportSTL(std::string file_path, std::string file_name) {
	//open file and write
	file_path.append(file_name);
	file_path.append(".stl");

	std::fstream file;
	file.open(file_path, std::ios::out);

	//Header
	file << "solid " << file_name << std::endl;

	for (UINT i = 0; i < HE_Triangles.size(); i++) {
		file << "facet normal " << HE_Triangles[i]->Normal.x << ' ' << HE_Triangles[i]->Normal.y << ' ' << HE_Triangles[i]->Normal.z << ' ' << std::endl;
		file << "outer loop " << std::endl;
		file << "vertex " << HE_Vertexes[HE_Triangles[i]->VertexIndex0]->Pos.x << ' ' << HE_Vertexes[HE_Triangles[i]->VertexIndex0]->Pos.y << ' ' << HE_Vertexes[HE_Triangles[i]->VertexIndex0]->Pos.z << ' ' << std::endl;
		file << "vertex " << HE_Vertexes[HE_Triangles[i]->VertexIndex1]->Pos.x << ' ' << HE_Vertexes[HE_Triangles[i]->VertexIndex1]->Pos.y << ' ' << HE_Vertexes[HE_Triangles[i]->VertexIndex1]->Pos.z << ' ' << std::endl;
		file << "vertex " << HE_Vertexes[HE_Triangles[i]->VertexIndex2]->Pos.x << ' ' << HE_Vertexes[HE_Triangles[i]->VertexIndex2]->Pos.y << ' ' << HE_Vertexes[HE_Triangles[i]->VertexIndex2]->Pos.z << ' ' << std::endl;
		file << "endloop" << std::endl;
		file << "endfacet" << std::endl;
	}

	file << "endsolid " << file_name << std::endl;
}



//Traverse==========================================================================================================================
HE_Triangle* HE_MeshData::CreateHE_Triangle(UINT index, DirectX::XMFLOAT3 normal, std::vector<std::int32_t>& VertexIndices) {
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

	for (UINT i = 0; i < edges.size(); i++) {
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

	for (std::int32_t i = 0; i < edges.size(); i++) {
		triangles.push_back(edges[i]->Triangle);
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



//Repair Normal==========================================================================================================================
void HE_MeshData::RepairNormal() {
	UINT correctFacetIndex = 0;
	bool allWrong = true;

	//找到一个正确面
	for (UINT i = 0; i < HE_Triangles.size(); i++) {
		//Möller-Trumbore
		if (GetIntersection(HE_Triangles[i]) % 2 == 0) {
			correctFacetIndex = i;
			allWrong = false;
			break;
		}
	}

	//如果全部面出错先改一个
	if (allWrong) {
		CorrectTriangle(HE_Triangles[correctFacetIndex]);
	}
	CorrectNormals(HE_Triangles[correctFacetIndex]);
}

int HE_MeshData::GetIntersection(HE_Triangle* triangle) {
	int ans = 0;

	float x_origin = (
		HE_Vertexes[triangle->VertexIndex0]->Pos.x +
		HE_Vertexes[triangle->VertexIndex1]->Pos.x +
		HE_Vertexes[triangle->VertexIndex2]->Pos.x
		) / 3;

	float y_origin = (
		HE_Vertexes[triangle->VertexIndex0]->Pos.y +
		HE_Vertexes[triangle->VertexIndex1]->Pos.y +
		HE_Vertexes[triangle->VertexIndex2]->Pos.y
		) / 3;

	float z_origin = (
		HE_Vertexes[triangle->VertexIndex0]->Pos.z +
		HE_Vertexes[triangle->VertexIndex1]->Pos.z +
		HE_Vertexes[triangle->VertexIndex2]->Pos.z
		) / 3;

	DirectX::XMFLOAT3 origin = DirectX::XMFLOAT3(x_origin, y_origin, z_origin);
	DirectX::XMFLOAT3 dir = triangle->Normal;

	for (UINT i = 0; i < HE_Triangles.size(); i++) {
		HE_Triangle* temp = HE_Triangles[i];

		if (temp != triangle) {
			DirectX::XMFLOAT3 v0 = HE_Vertexes[temp->VertexIndex0]->Pos;
			DirectX::XMFLOAT3 v1 = HE_Vertexes[temp->VertexIndex1]->Pos;
			DirectX::XMFLOAT3 v2 = HE_Vertexes[temp->VertexIndex2]->Pos;

			DirectX::XMFLOAT3 S = MathHelper::SubVector(origin, v0);
			DirectX::XMFLOAT3 E1 = MathHelper::SubVector(v1, v0);
			DirectX::XMFLOAT3 E2 = MathHelper::SubVector(v2, v0);

			DirectX::XMFLOAT3 S1 = MathHelper::CrossVector(dir, E2);
			DirectX::XMFLOAT3 S2 = MathHelper::CrossVector(S, E1);

			float S1E1 = MathHelper::DotVector(S1, E1);
			float t = MathHelper::DotVector(S2, E2) / S1E1;
			float b1 = MathHelper::DotVector(S1, S) / S1E1;
			float b2 = MathHelper::DotVector(S2, dir) / S1E1;

			if (t >= 0.0f && b1 >= 0.0f && b2 >= 0.0f && (1 - b1 - b2) >= 0.0f) {
				ans++;
			}
		}
	}
	return ans;
}

void HE_MeshData::CorrectTriangle(HE_Triangle* wrongTriangle) {
	//更改顶点顺序
	std::swap(wrongTriangle->VertexIndex1, wrongTriangle->VertexIndex2);

	//重新计算法线
	wrongTriangle->Normal = MathHelper::CalTriNormal(
		HE_Vertexes[wrongTriangle->VertexIndex0]->Pos,
		HE_Vertexes[wrongTriangle->VertexIndex1]->Pos,
		HE_Vertexes[wrongTriangle->VertexIndex2]->Pos
	);

	std::vector<UINT> vertexIndices;
	vertexIndices.push_back(wrongTriangle->VertexIndex0);
	vertexIndices.push_back(wrongTriangle->VertexIndex1);
	vertexIndices.push_back(wrongTriangle->VertexIndex2);

	for (int i = 0; i < 3; i++) {
		std::int32_t u = vertexIndices[i % 3];
		std::int32_t v = vertexIndices[(i + 1) % 3];

		std::int32_t u_Next = vertexIndices[(i + 1) % 3];
		std::int32_t v_Next = vertexIndices[(i + 2) % 3];

		std::int32_t u_Pre = vertexIndices[(i + 2) % 3];
		std::int32_t v_Pre = vertexIndices[(i + 3) % 3];

		wrongTriangle->Edge = HE_Edges[std::make_pair(u, v)];
		wrongTriangle->Edge->Next = HE_Edges[std::make_pair(u_Next, v_Next)];
		wrongTriangle->Edge->Pre =HE_Edges[std::make_pair(u_Pre, v_Pre)];

		//如果pair是错边
		if (wrongTriangle->Edge->Pair->MarkTwice) {
			wrongTriangle->Edge->Pair->MarkTwice = false;

			//如果edge的面不是错面→pair的面是错面，edge是对面，交换
			if (wrongTriangle->Edge->Triangle != wrongTriangle) {
				wrongTriangle->Edge->Pair->Triangle = wrongTriangle->Edge->Triangle;
				wrongTriangle->Edge->Triangle = wrongTriangle;
			}
		}
		else {
			wrongTriangle->Edge->MarkTwice = true;
			wrongTriangle->Edge->Pair->Triangle = wrongTriangle->Edge->Triangle;
			wrongTriangle->Edge->Triangle = wrongTriangle;
		}

		wrongTriangle->Edge = wrongTriangle->Edge->Next;
	}
}

void HE_MeshData::CorrectNormals(HE_Triangle* correctTriangle) {
	std::queue<HE_Triangle*> qfacets;

	CorrectTrianglesFromTriangle(correctTriangle, qfacets);

	while (!qfacets.empty()) {
		HE_Triangle* temp = qfacets.front();
		qfacets.pop();

		CorrectTrianglesFromTriangle(temp, qfacets);
	}
}

void HE_MeshData::CorrectTrianglesFromTriangle(HE_Triangle* triangle, std::queue<HE_Triangle*>& qfacets) {
	triangle->IsMarked = true;

	std::vector<std::int32_t> vertexIndices;
	vertexIndices.push_back(triangle->VertexIndex0);
	vertexIndices.push_back(triangle->VertexIndex1);
	vertexIndices.push_back(triangle->VertexIndex2);


	for (int i = 0; i < 3; i++) {
		std::int32_t u = vertexIndices[i % 3];
		std::int32_t v = vertexIndices[(i + 1) % 3];
		std::int32_t u_Next = vertexIndices[(i + 1) % 3];
		std::int32_t v_Next = vertexIndices[(i + 2) % 3];
		std::int32_t u_Pre = vertexIndices[(i + 2) % 3];
		std::int32_t v_Pre = vertexIndices[(i + 3) % 3];

		HE_Edge* edge = HE_Edges[std::make_pair(u, v)];

		if (edge->MarkTwice) {
			if (edge->Pair->Triangle != triangle) {
				CorrectTriangle(edge->Pair->Triangle);
			}
			else {
				CorrectTriangle(edge->Triangle);
			}
		}

		edge->Next = HE_Edges[std::make_pair(u_Next, v_Next)];
		edge->Pre = HE_Edges[std::make_pair(u_Pre, v_Pre)];
		edge->Triangle = triangle;

		if (edge->Pair->Triangle != nullptr && !edge->Pair->Triangle->IsMarked) {
			qfacets.push(edge->Pair->Triangle);
			edge->Pair->Triangle->IsMarked = true;
		}

	}
}



//Denoise==========================================================================================================================
void HE_MeshData::Denoise(int denoiseNum, float T) {
	for (int i = 0; i < denoiseNum; i++){
		DenoiseHelper(T);
	}
}

void HE_MeshData::DenoiseHelper(float T) {
	std::vector<DirectX::XMFLOAT3> expect_normals;

	for (UINT i = 0; i < HE_Triangles.size(); i++){
		std::vector<HE_Triangle*> neighbors = GetTrianglesFromTriangle(HE_Triangles[i]);
		DirectX::XMFLOAT3 normal_expect = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);

		/*
		int neighbor_size = neighbors.size();

		for (int j = 0; j < neighbor_size; j++){
			std::vector<HE_Triangle*> temp = GetTrianglesFromTriangle(neighbors[j]);

			for (int k = 0; k < temp.size(); k++){
				bool flag = true;

				for (int m = 0; m < neighbors.size(); m++){
					if (temp[k] == neighbors[m] || temp[k] == HE_Triangles[i]) {
						flag = false;
						break;
					}
				}

				if (flag) {
					neighbors.push_back(temp[k]);
				}
			}
		}
		*/

		for (int j = 0; j < neighbors.size(); j++){
			float temp = MathHelper::DotVector(HE_Triangles[i]->Normal, neighbors[j]->Normal);
			float weight = temp > T ? pow(temp - T, 2) : 0;

			normal_expect.x += weight * neighbors[j]->Normal.x;
			normal_expect.y += weight * neighbors[j]->Normal.y;
			normal_expect.z += weight * neighbors[j]->Normal.z;
		}

		expect_normals.push_back(normal_expect);
	}

	for (std::int32_t i = 0; i < HE_Vertexes.size(); i++){
		std::vector<HE_Triangle*> neighbors = GetTrianglesFromVertex(HE_Vertexes[i]);
		DirectX::XMFLOAT3 move = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);

		for (int j = 0; j < neighbors.size(); j++){
			DirectX::XMFLOAT3 centroid = DirectX::XMFLOAT3(
				(HE_Vertexes[neighbors[j]->VertexIndex0]->Pos.x + HE_Vertexes[neighbors[j]->VertexIndex1]->Pos.x + HE_Vertexes[neighbors[j]->VertexIndex2]->Pos.x) / 3,
				(HE_Vertexes[neighbors[j]->VertexIndex0]->Pos.y + HE_Vertexes[neighbors[j]->VertexIndex1]->Pos.y + HE_Vertexes[neighbors[j]->VertexIndex2]->Pos.y) / 3,
				(HE_Vertexes[neighbors[j]->VertexIndex0]->Pos.z + HE_Vertexes[neighbors[j]->VertexIndex1]->Pos.z + HE_Vertexes[neighbors[j]->VertexIndex2]->Pos.z) / 3
			);

			DirectX::XMFLOAT3 diff = DirectX::XMFLOAT3(
				centroid.x - HE_Vertexes[i]->Pos.x, centroid.y - HE_Vertexes[i]->Pos.y, centroid.z - HE_Vertexes[i]->Pos.z
			);

			float weight = MathHelper::DotVector(expect_normals[neighbors[j]->Index], diff);

			DirectX::XMFLOAT3 step = DirectX::XMFLOAT3(
				weight * expect_normals[neighbors[j]->Index].x, weight * expect_normals[neighbors[j]->Index].y, weight * expect_normals[neighbors[j]->Index].z
			);

			move.x += step.x; move.y += step.y; move.z += step.z;
		}

		move.x /= neighbors.size(); move.y /= neighbors.size(); move.z /= neighbors.size();
		HE_Vertexes[i]->Pos.x = HE_Vertexes[i]->Pos.x + move.x;
		HE_Vertexes[i]->Pos.y = HE_Vertexes[i]->Pos.y + move.y;
		HE_Vertexes[i]->Pos.z = HE_Vertexes[i]->Pos.z + move.z;
	}

	for (UINT i = 0; i < HE_Triangles.size(); i++) {
		DirectX::XMFLOAT3 correctNormal = MathHelper::CalTriNormal(
			HE_Vertexes[HE_Triangles[i]->VertexIndex0]->Pos,
			HE_Vertexes[HE_Triangles[i]->VertexIndex1]->Pos,
			HE_Vertexes[HE_Triangles[i]->VertexIndex2]->Pos
		);
		HE_Triangles[i]->Normal = correctNormal;
	}
}

//Smoothing==========================================================================================================================
void HE_MeshData::Smoothing(int fairingNum, FMETHOD fairing) {
	for (int i = 0; i < fairingNum; i++){
		Fairing(fairing);
	}

	for (UINT i = 0; i < HE_Triangles.size(); i++) {
		DirectX::XMFLOAT3 correctNormal = MathHelper::CalTriNormal(
			HE_Vertexes[HE_Triangles[i]->VertexIndex0]->Pos,
			HE_Vertexes[HE_Triangles[i]->VertexIndex1]->Pos,
			HE_Vertexes[HE_Triangles[i]->VertexIndex2]->Pos
		);
		HE_Triangles[i]->Normal = correctNormal;
	}
}



//Holes Filling==========================================================================================================================
void HE_MeshData::HolesFilling(METHOD method, RMETHOD refinement, FMETHOD fairing, int fairingNum) {
	HolesIdentification();

	for (auto boundaryLoop : BoundaryLoops){
		HolesFillingHelper(boundaryLoop, method, refinement, fairing, fairingNum);
	}
}

void HE_MeshData::HolesIdentification() {
	std::map<std::int32_t, std::int32_t> incorrectEdgeIndices;

	for (auto edge = HE_Edges.begin(); edge != HE_Edges.end(); edge++){
		if (edge->second->Triangle == nullptr) {
			incorrectEdgeIndices[edge->first.first] = edge->first.second;
		}
	}

	int currentVertex = -1;
	std::vector<std::int32_t> boundaryLoop(incorrectEdgeIndices.size());

	while (true) {
		if (currentVertex == -1) {
			if (incorrectEdgeIndices.empty()) {
				break;
			}

			currentVertex = incorrectEdgeIndices.begin()->first;
			boundaryLoop.clear();
			boundaryLoop.push_back(currentVertex);
		}
		else {
			std::int32_t neighborVertex = incorrectEdgeIndices[currentVertex];
			incorrectEdgeIndices.erase(currentVertex);

			if (neighborVertex == boundaryLoop[0]) {
				reverse(boundaryLoop.begin(), boundaryLoop.end());
				BoundaryLoops.push_back(boundaryLoop);
				currentVertex = -1;
			}
			else {
				boundaryLoop.push_back(neighborVertex);
				currentVertex = neighborVertex;
			}
		}
	}

}

void HE_MeshData::HolesFillingHelper(std::vector<std::int32_t>& boundaryLoop, METHOD method, RMETHOD refinement, FMETHOD fairing, int fairingNum) {
	std::vector<std::vector<std::int32_t>> triangles;
	std::vector<std::vector<float>> A;
	std::vector<std::vector<std::int32_t>> ls;
	
	for (int i = boundaryLoop.size() - 1; i > 0; i--){
		std::vector<float> vec(i);
		A.push_back(vec);

		if (i >= boundaryLoop.size() - 2){
			std::vector<std::int32_t> v;
			ls.push_back(v);
		}
		else{
			std::vector<std::int32_t> v(i);
			ls.push_back(v);
		}
	}

	//Triangulation
	if (method == ANGLE) {
		
	}
	else if(method == AREA) {
		for (std::int32_t i = 3; i < boundaryLoop.size(); i++){
			for (std::int32_t j = 0; j < boundaryLoop.size() - i; j++){

				float minArea = FLT_MAX;
				std::int32_t KoPt = INT_MIN;

				for (int k = 0; k < i - 1; k++){
					DirectX::XMFLOAT3 v0 = HE_Vertexes[boundaryLoop[j]]->Pos;
					DirectX::XMFLOAT3 v1 = HE_Vertexes[boundaryLoop[k + j + 1]]->Pos;
					DirectX::XMFLOAT3 v2 = HE_Vertexes[boundaryLoop[j + i]]->Pos;

					float area = MathHelper::GetTriArea(v0, v1, v2) + A[k][j] + A[i - k - 2][k + j + 1];
					
					if (area < minArea) {
						KoPt = k;
						minArea = area;
					}
				}

				ls[i - 1][j] = KoPt + j + 1;
				A[i - 1][j] = minArea;

			}
		}
	}

	Refinement(boundaryLoop, ls, triangles, refinement);

	InsertTriangles(triangles);

	for (int i = 0; i < fairingNum; i++) {
		Fairing(fairing);
	}

	for (UINT i = 0; i < HE_Triangles.size(); i++) {
		DirectX::XMFLOAT3 correctNormal = MathHelper::CalTriNormal(
			HE_Vertexes[HE_Triangles[i]->VertexIndex0]->Pos,
			HE_Vertexes[HE_Triangles[i]->VertexIndex1]->Pos,
			HE_Vertexes[HE_Triangles[i]->VertexIndex2]->Pos
		);
		HE_Triangles[i]->Normal = correctNormal;
	}
}


void HE_MeshData::Refinement(std::vector<std::int32_t>& boundaryLoop, std::vector<std::vector<std::int32_t>>& ls, std::vector<std::vector<std::int32_t>>& triangles, RMETHOD refinement) {
	std::vector<DirectX::XMFLOAT3> centroids;
	
	TrianglesToBeInserted(boundaryLoop, ls, centroids, triangles);

	if (refinement == NO_REFINEMENT) {
		return;
	}

	std::unordered_map<std::int32_t, float> sigmas;
	CalculateSigmas(boundaryLoop, sigmas);

	std::vector<float> centroidSigmas;
	FetchCentroidSigmas(triangles, centroidSigmas, sigmas);

	int protect = 0;

	while (protect++ < 2) {
		std::vector<std::int32_t> trianglesToBeDivided;
		IdentifyDividedTriangles(triangles, centroids, sigmas, centroidSigmas, trianglesToBeDivided);

		if (trianglesToBeDivided.empty()) break;

		std::vector<std::pair<std::int32_t, std::int32_t>> edgesToBeRelaxed;
		DivideTriangles(triangles, trianglesToBeDivided, centroids, sigmas, centroidSigmas, edgesToBeRelaxed);

		for (int i = 0; i < edgesToBeRelaxed.size(); i++) {
			RelaxEdge(triangles, edgesToBeRelaxed[i].first, edgesToBeRelaxed[i].second, centroids, sigmas, centroidSigmas);
		}

		std::int32_t numOfSwap = INT_MAX;
		std::int32_t sameCounter = 0;

		while (numOfSwap && sameCounter < 20) {
			Relax(triangles, numOfSwap, sameCounter, centroids, sigmas, centroidSigmas);
		}
	}

}

void HE_MeshData::TrianglesToBeInserted(std::vector<std::int32_t>& boundaryLoop, std::vector<std::vector<std::int32_t>>& ls, std::vector<DirectX::XMFLOAT3>& centroids, std::vector<std::vector<std::int32_t>>& triangles) {
	std::vector<std::pair<std::int32_t, std::int32_t>> sections;

	sections.push_back(std::pair<std::int32_t, std::int32_t>{0, boundaryLoop.size() - 1});

	while (sections.size() > 0) {
		std::pair<std::int32_t, std::int32_t> section = sections.back();
		sections.pop_back();
		int k;

		if (section.second - section.first == 2) {
			k = section.first + 1;
		}
		else {
			k = ls[section.second - section.first - 1][section.first];
		}

		float x =
				(HE_Vertexes[boundaryLoop[section.first]]->Pos.x +
				HE_Vertexes[boundaryLoop[k]]->Pos.x +
				HE_Vertexes[boundaryLoop[section.second]]->Pos.x) / 3;
		float y =
				(HE_Vertexes[boundaryLoop[section.first]]->Pos.y +
				HE_Vertexes[boundaryLoop[k]]->Pos.y +
				HE_Vertexes[boundaryLoop[section.second]]->Pos.y) / 3;
		float z =
				(HE_Vertexes[boundaryLoop[section.first]]->Pos.z +
				HE_Vertexes[boundaryLoop[k]]->Pos.z +
				HE_Vertexes[boundaryLoop[section.second]]->Pos.z) / 3;

		centroids.push_back(DirectX::XMFLOAT3(x, y, z));

		std::vector<std::int32_t> temp;
		temp.push_back(boundaryLoop[section.first]);
		temp.push_back(boundaryLoop[k]);
		temp.push_back(boundaryLoop[section.second]);

		triangles.push_back(temp);

		if (k - section.first > 1){
			sections.push_back(std::pair<std::int32_t, std::int32_t>{ section.first, k });
		}

		if (section.second - k > 1){
			sections.push_back(std::pair<std::int32_t, std::int32_t>{ k, section.second });
		}
	}
}

void HE_MeshData::CalculateSigmas(std::vector<std::int32_t>& boundaryLoop, std::unordered_map<std::int32_t, float>& sigmas) {
	for (int i = 0; i < boundaryLoop.size(); i++){
		sigmas[boundaryLoop[i]] = 0;
		HE_Vertex* temp = HE_Vertexes[boundaryLoop[i]];
		std::vector<HE_Vertex*> neighbors = GetVertexesFromVertex(HE_Vertexes[boundaryLoop[i]]);

		for (int j = 0; j < neighbors.size(); j++){
			HE_Vertex* adj = neighbors[j];
			sigmas[boundaryLoop[i]] += sqrt(pow(temp->Pos.x - adj->Pos.x, 2) + pow(temp->Pos.y - adj->Pos.y, 2) + pow(temp->Pos.z - adj->Pos.z, 2));
		}

		sigmas[boundaryLoop[i]] /= neighbors.size();
	}
}

void HE_MeshData::FetchCentroidSigmas(std::vector<std::vector<std::int32_t>>& triangles, std::vector<float>& centroidSigmas, std::unordered_map<std::int32_t, float>& sigmas) {
	for (int i = 0; i < triangles.size(); i++){
		std::int32_t v0 = triangles[i][2];
		std::int32_t v1 = triangles[i][1];
		std::int32_t v2 = triangles[i][0];

		centroidSigmas.push_back((sigmas[v0] + sigmas[v1] + sigmas[v2]) / 3.0);
	}
}


void HE_MeshData::IdentifyDividedTriangles(std::vector<std::vector<std::int32_t>>& triangles, std::vector<DirectX::XMFLOAT3>& centroids, std::unordered_map<std::int32_t, float>& sigmas, std::vector<float>& centroidSigmas, std::vector<std::int32_t>& trianglesToBeDivided) {
	for (int i = 0; i < triangles.size(); i++){
		float sigma_vertex_centroid = centroidSigmas[i];
		DirectX::XMFLOAT3 vertex_centroid = centroids[i];
		
		for (int j = 0; j < 3; j++){
			DirectX::XMFLOAT3 vertex = HE_Vertexes[triangles[i][j]]->Pos;

			DirectX::XMFLOAT3 diff = DirectX::XMFLOAT3(vertex_centroid.x - vertex.x, vertex_centroid.y - vertex.y, vertex_centroid.z - vertex.z);
			float diff_len = sqrt(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z);

			float sigma_vertex = sigmas[triangles[i][j]];

			if (sqrt(2) * diff_len > sigma_vertex_centroid && sqrt(2) * diff_len > sigma_vertex){
				trianglesToBeDivided.push_back(i);
				break;
			}
		}
	}
}


void HE_MeshData::DivideTriangles(std::vector<std::vector<std::int32_t>>& triangles, std::vector<std::int32_t>& trianglesToBeDivided, std::vector<DirectX::XMFLOAT3>& centroids, std::unordered_map<std::int32_t, float>& sigmas, std::vector<float>& centroidSigmas, std::vector<std::pair<std::int32_t, std::int32_t>>& edgesToBeRelaxed) {
	for (int i = 0; i < trianglesToBeDivided.size(); i++){
		DirectX::XMFLOAT3 coord = centroids[trianglesToBeDivided[i]];

		HE_Vertex* centroid = new HE_Vertex();
		centroid->Index = HE_Vertexes.size();
		centroid->Pos = coord;
		centroid->Normal = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
		HE_Vertexes.push_back(centroid);

		edgesToBeRelaxed.push_back(std::pair<std::int32_t, std::int32_t>
			(triangles[trianglesToBeDivided[i]][0], triangles[trianglesToBeDivided[i]][1]));
		edgesToBeRelaxed.push_back(std::pair<std::int32_t, std::int32_t>
			(triangles[trianglesToBeDivided[i]][1], triangles[trianglesToBeDivided[i]][2]));
		edgesToBeRelaxed.push_back(std::pair<std::int32_t, std::int32_t>
			(triangles[trianglesToBeDivided[i]][2], triangles[trianglesToBeDivided[i]][0]));

		std::vector<std::int32_t> tri0;
		tri0.push_back(HE_Vertexes.size() - 1);
		tri0.push_back(triangles[trianglesToBeDivided[i]][1]);
		tri0.push_back(triangles[trianglesToBeDivided[i]][2]);

		std::vector<std::int32_t> tri1;
		tri1.push_back(triangles[trianglesToBeDivided[i]][0]);
		tri1.push_back(HE_Vertexes.size() - 1);
		tri1.push_back(triangles[trianglesToBeDivided[i]][2]);

		std::vector<std::int32_t> tri2;
		tri2.push_back(triangles[trianglesToBeDivided[i]][0]);
		tri2.push_back(triangles[trianglesToBeDivided[i]][1]);
		tri2.push_back(HE_Vertexes.size() - 1);

		triangles[trianglesToBeDivided[i]] = tri0;
		triangles.push_back(tri1);
		triangles.push_back(tri2);

		sigmas[HE_Vertexes.size() - 1] = centroidSigmas[trianglesToBeDivided[i]];

		DirectX::XMFLOAT3 centroid0 = DirectX::XMFLOAT3(
			(HE_Vertexes[tri0[0]]->Pos.x + HE_Vertexes[tri0[1]]->Pos.x + HE_Vertexes[tri0[2]]->Pos.x) / 3.0f,
			(HE_Vertexes[tri0[0]]->Pos.y + HE_Vertexes[tri0[1]]->Pos.y + HE_Vertexes[tri0[2]]->Pos.y) / 3.0f,
			(HE_Vertexes[tri0[0]]->Pos.z + HE_Vertexes[tri0[1]]->Pos.z + HE_Vertexes[tri0[2]]->Pos.z) / 3.0f
		);
		DirectX::XMFLOAT3 centroid1 = DirectX::XMFLOAT3(
			(HE_Vertexes[tri1[0]]->Pos.x + HE_Vertexes[tri1[1]]->Pos.x + HE_Vertexes[tri1[2]]->Pos.x) / 3.0f,
			(HE_Vertexes[tri1[0]]->Pos.y + HE_Vertexes[tri1[1]]->Pos.y + HE_Vertexes[tri1[2]]->Pos.y) / 3.0f,
			(HE_Vertexes[tri1[0]]->Pos.z + HE_Vertexes[tri1[1]]->Pos.z + HE_Vertexes[tri1[2]]->Pos.z) / 3.0f
		);
		DirectX::XMFLOAT3 centroid2 = DirectX::XMFLOAT3(
			(HE_Vertexes[tri2[0]]->Pos.x + HE_Vertexes[tri2[1]]->Pos.x + HE_Vertexes[tri2[2]]->Pos.x) / 3.0f,
			(HE_Vertexes[tri2[0]]->Pos.y + HE_Vertexes[tri2[1]]->Pos.y + HE_Vertexes[tri2[2]]->Pos.y) / 3.0f,
			(HE_Vertexes[tri2[0]]->Pos.z + HE_Vertexes[tri2[1]]->Pos.z + HE_Vertexes[tri2[2]]->Pos.z) / 3.0f
		);

		centroidSigmas[trianglesToBeDivided[i]] = (sigmas[tri0[0]] + sigmas[tri0[1]] + sigmas[tri0[2]]) / 3.0;
		centroidSigmas.push_back((sigmas[tri1[0]] + sigmas[tri1[1]] + sigmas[tri1[2]]) / 3.0);
		centroidSigmas.push_back((sigmas[tri2[0]] + sigmas[tri2[1]] + sigmas[tri2[2]]) / 3.0);

		centroids[trianglesToBeDivided[i]] = centroid0;
		centroids.push_back(centroid1);
		centroids.push_back(centroid2);

	}
}


void HE_MeshData::RelaxEdge(std::vector<std::vector<std::int32_t>>& triangles, std::int32_t v0, std::int32_t v1, std::vector<DirectX::XMFLOAT3>& centroids, std::unordered_map<std::int32_t, float>& sigmas, std::vector<float>& centroidSigmas) {
	std::vector<std::int32_t> tris;

	for (int i = 0; i < triangles.size(); i++){
		std::unordered_set<std::int32_t> tri;
		tri.insert(triangles[i][0]);
		tri.insert(triangles[i][1]);
		tri.insert(triangles[i][2]);

		if (tri.count(v0) > 0 && tri.count(v1) > 0){
			tris.push_back(i);
		}
	}

	if (tris.size() != 2)
		return;

	int op0 = -1;
	for (int i = 0; i < 3; i++){
		if (triangles[tris[0]][i] != v0 && triangles[tris[0]][i] != v1) {
			op0 = triangles[tris[0]][i];
			break;
		}
	}

	int op1 = -1;
	for (int i = 0; i < 3; i++){
		if (triangles[tris[1]][i] != v0 && triangles[tris[1]][i] != v1){
			op1 = triangles[tris[1]][i];
			break;
		}
	}

	if (op0 == -1 || op1 == -1)
		return;

	if (CircumSphereCheck(v0, v1, op0, op1) || CircumSphereCheck(v0, v1, op1, op0)){
		if (SwapCheck(triangles, v0, v1, op0, op1)){
			triangles[tris[0]][0] = op0; 
			triangles[tris[0]][1] = op1; 
			triangles[tris[0]][2] = v0;

			triangles[tris[1]][0] = op0;
			triangles[tris[1]][1] = op1;
			triangles[tris[1]][2] = v1;
			
			centroids[tris[0]] = DirectX::XMFLOAT3(
				(HE_Vertexes[op0]->Pos.x + HE_Vertexes[op1]->Pos.x + HE_Vertexes[v0]->Pos.x) / 3.0f,
				(HE_Vertexes[op0]->Pos.y + HE_Vertexes[op1]->Pos.y + HE_Vertexes[v0]->Pos.y) / 3.0f,
				(HE_Vertexes[op0]->Pos.z + HE_Vertexes[op1]->Pos.z + HE_Vertexes[v0]->Pos.z) / 3.0f
			);

			centroids[tris[1]] = DirectX::XMFLOAT3(
				(HE_Vertexes[op0]->Pos.x + HE_Vertexes[op1]->Pos.x + HE_Vertexes[v1]->Pos.x) / 3.0f,
				(HE_Vertexes[op0]->Pos.y + HE_Vertexes[op1]->Pos.y + HE_Vertexes[v1]->Pos.y) / 3.0f,
				(HE_Vertexes[op0]->Pos.z + HE_Vertexes[op1]->Pos.z + HE_Vertexes[v1]->Pos.z) / 3.0f
			);

			centroidSigmas[tris[0]] = (sigmas[op0] + sigmas[op1] + sigmas[v0]) / 3.0;
			centroidSigmas[tris[1]] = (sigmas[op0] + sigmas[op1] + sigmas[v1]) / 3.0;
		}
	}
}

bool HE_MeshData::CircumSphereCheck(std::int32_t v0_, std::int32_t v1_, std::int32_t v2_, std::int32_t op_) {
	Eigen::Vector3f v0;
	v0 << HE_Vertexes[v0_]->Pos.x, HE_Vertexes[v0_]->Pos.y, HE_Vertexes[v0_]->Pos.z;

	Eigen::Vector3f v1;
	v1 << HE_Vertexes[v1_]->Pos.x, HE_Vertexes[v1_]->Pos.y, HE_Vertexes[v1_]->Pos.z;

	Eigen::Vector3f v2;
	v2 << HE_Vertexes[v2_]->Pos.x, HE_Vertexes[v2_]->Pos.y, HE_Vertexes[v2_]->Pos.z;

	Eigen::Vector3f op;
	op << HE_Vertexes[op_]->Pos.x, HE_Vertexes[op_]->Pos.y, HE_Vertexes[op_]->Pos.z;

	if ((v0 - v1).norm() < (v2 - op).norm())
		return false;

	Eigen::Matrix<float, 2, 3> projection;
	Eigen::Vector3f v10 = (v1 - v0).normalized();
	Eigen::Vector3f v = v2 - v0;
	Eigen::Vector3f n = v.cross(v10);
	Eigen::Vector3f v20 = v10.cross(n).normalized();

	projection.row(0) = v10;
	projection.row(1) = v20;

	Eigen::Vector2f A;
	A.fill(0);

	Eigen::Matrix<float, 2, 1> B = projection * (v1 - v0);
	Eigen::Matrix<float, 2, 1> C = projection * (v2 - v0);
	Eigen::Matrix<float, 2, 1> v11 = projection * (op - v0);

	Eigen::Matrix4d M;
	M(0, 0) = v11.squaredNorm();
	M(1, 0) = A.squaredNorm();
	M(2, 0) = B.col(0).squaredNorm();
	M(3, 0) = C.col(0).squaredNorm();

	M(0, 1) = v11(0);
	M(1, 1) = A(0);
	M(2, 1) = B(0);
	M(3, 1) = C(0);

	M(0, 2) = v11(1);
	M(1, 2) = A(1);
	M(2, 2) = B(1);
	M(3, 2) = C(1);

	M(0, 3) = 1;
	M(1, 3) = 1;
	M(2, 3) = 1;
	M(3, 3) = 1;

	if (M.determinant() > 0)
		return false;
	else
		return true;
}

bool HE_MeshData::SwapCheck(const std::vector<std::vector<std::int32_t>>& triangles, std::int32_t iv0, std::int32_t iv1, std::int32_t op0, std::int32_t op1) {
	std::unordered_set<std::int32_t> tri0, tri1;
	tri0.insert(iv0);
	tri0.insert(op1);
	tri0.insert(op0);

	tri1.insert(iv1);
	tri1.insert(op0);
	tri1.insert(op1);

	for (int i = 0; i < triangles.size(); i++){
		if (tri0.count(triangles[i][0]) > 0 && tri0.count(triangles[i][1]) > 0 && tri0.count(triangles[i][2]) > 0)
			return false;

		if (tri1.count(triangles[i][0]) > 0 && tri1.count(triangles[i][1]) > 0 && tri1.count(triangles[i][2]) > 0)
			return false;
	}

	return true;
}


void HE_MeshData::Relax(std::vector<std::vector<std::int32_t>>& triangles, std::int32_t& numOfSwap, std::int32_t& sameCounter, std::vector<DirectX::XMFLOAT3>& centroids, std::unordered_map<std::int32_t, float>& sigmas, std::vector<float>& centroidSigmas) {
	std::unordered_map<TempEdge, std::int32_t> edgesToTriangles;
	std::unordered_set<std::int32_t> swapSet;
	std::vector<SwapS> toBeSwapped;

	for (int i = 0; i < triangles.size(); i++){
		if (swapSet.count(i))
			continue;

		if (EdgeCheck(triangles, edgesToTriangles, 0, 1, 2, i, swapSet, toBeSwapped)) continue;
		if (EdgeCheck(triangles, edgesToTriangles, 1, 2, 0, i, swapSet, toBeSwapped)) continue;
		if (EdgeCheck(triangles, edgesToTriangles, 2, 0, 1, i, swapSet, toBeSwapped)) continue;
	}

	for (int i = 0; i < toBeSwapped.size(); i++){
		Swap(triangles, toBeSwapped[i].tri0, toBeSwapped[i].op0, toBeSwapped[i].tri1, toBeSwapped[i].op1, toBeSwapped[i].iv0, toBeSwapped[i].iv1, centroids, sigmas, centroidSigmas);
	}

	if (toBeSwapped.size() == numOfSwap)
		sameCounter++;

	numOfSwap = toBeSwapped.size();
}

bool HE_MeshData::EdgeCheck(std::vector<std::vector<std::int32_t>>& triangles, std::unordered_map<TempEdge, std::int32_t>& edgesToTriangles, std::int32_t v0, std::int32_t v1, std::int32_t v2, std::int32_t i, std::unordered_set<std::int32_t>& swapSet, std::vector<SwapS>& toBeSwapped) {
	std::int32_t a, b;

	if ((a = edgesToTriangles.count({ triangles[i][v0],  triangles[i][v1] })) == 0 && 
		(b = edgesToTriangles.count({ triangles[i][v1],  triangles[i][v0] })) == 0) {
		edgesToTriangles[{triangles[i][v0], triangles[i][v1]}] = i;
	}
	else{
		int neighbor, opposite = -1;

		neighbor = 
			(a > 0) ? 
			edgesToTriangles[{ triangles[i][v0], triangles[i][v1] }] : 
			edgesToTriangles[{ triangles[i][v1], triangles[i][v0] }];

		if (swapSet.count(neighbor))
			return false;

		for (int k = 0; k < 3; k++){
			if (triangles[neighbor][k] != triangles[i][v0] && triangles[neighbor][k] != triangles[i][v1]){
				opposite = triangles[neighbor][k];
				break;
			}
		}

		if (opposite == -1){
			return false;
		}

		if (CircumSphereCheck(triangles[i][v0], triangles[i][v1], triangles[i][v2], opposite) || 
			CircumSphereCheck(triangles[i][v0], triangles[i][v1], opposite, triangles[i][v2])){
			swapSet.insert(i);
			swapSet.insert(neighbor);
			toBeSwapped.push_back({ i, triangles[i][v2], neighbor, opposite, triangles[i][v0], triangles[i][v1]});
			return true;
		}
	}

	return false;
}

void HE_MeshData::Swap(std::vector<std::vector<std::int32_t>>& triangles, std::int32_t tri0, std::int32_t op0, std::int32_t tri1, std::int32_t op1, std::int32_t iv0, std::int32_t iv1, std::vector<DirectX::XMFLOAT3>& centroids, std::unordered_map<std::int32_t, float>& sigmas, std::vector<float>& centroidSigmas) {
	if (SwapCheck(triangles, iv0, iv1, op0, op1)){
		triangles[tri0][0] = iv0;
		triangles[tri0][1] = op1;
		triangles[tri0][2] = op0;

		triangles[tri1][0] = iv1;
		triangles[tri1][1] = op0;
		triangles[tri1][2] = op1;

		centroids[tri0] = DirectX::XMFLOAT3(
			(HE_Vertexes[op0]->Pos.x + HE_Vertexes[op1]->Pos.x + HE_Vertexes[iv0]->Pos.x) / 3.0f,
			(HE_Vertexes[op0]->Pos.y + HE_Vertexes[op1]->Pos.y + HE_Vertexes[iv0]->Pos.y) / 3.0f,
			(HE_Vertexes[op0]->Pos.z + HE_Vertexes[op1]->Pos.z + HE_Vertexes[iv0]->Pos.z) / 3.0f
		);

		centroids[tri1] = DirectX::XMFLOAT3(
			(HE_Vertexes[op0]->Pos.x + HE_Vertexes[op1]->Pos.x + HE_Vertexes[iv1]->Pos.x) / 3.0f,
			(HE_Vertexes[op0]->Pos.y + HE_Vertexes[op1]->Pos.y + HE_Vertexes[iv1]->Pos.y) / 3.0f,
			(HE_Vertexes[op0]->Pos.z + HE_Vertexes[op1]->Pos.z + HE_Vertexes[iv1]->Pos.z) / 3.0f
		);

		centroidSigmas[tri0] = (sigmas[op0] + sigmas[op1] + sigmas[iv0]) / 3.0;
		centroidSigmas[tri1] = (sigmas[op0] + sigmas[op1] + sigmas[iv1]) / 3.0;
	}
}


void HE_MeshData::InsertTriangles(std::vector<std::vector<std::int32_t>>& triangles) {
	for (int i = 0; i < triangles.size(); i++){
		DirectX::XMFLOAT3 correctNormal = MathHelper::CalTriNormal(
			HE_Vertexes[triangles[i][0]]->Pos,
			HE_Vertexes[triangles[i][1]]->Pos,
			HE_Vertexes[triangles[i][2]]->Pos
		);

		std::vector<std::int32_t> vertex_indices;
		vertex_indices.push_back(triangles[i][0]);
		vertex_indices.push_back(triangles[i][1]);
		vertex_indices.push_back(triangles[i][2]);

		HE_Triangle* temp = CreateHE_Triangle(HE_Triangles.size(), correctNormal, vertex_indices);
		HE_Triangles.push_back(temp);
		New_HE_Triangles.push_back(temp);
	}

}


void HE_MeshData::Fairing(enum FMETHOD fairing) {
	if (fairing == NO_FAIRING) {
		return;
	}

	for (std::int32_t i = 0; i < HE_Vertexes.size(); i++) {
		HE_Vertex* temp = HE_Vertexes[i];
		std::vector<HE_Vertex*> neighbors = GetVertexesFromVertex(temp);

		float weightSum = 0.0f;
		DirectX::XMFLOAT3 step = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
		DirectX::XMFLOAT3 move = DirectX::XMFLOAT3(-temp->Pos.x, -temp->Pos.y, -temp->Pos.z);
		
		for (int j = 0; j < neighbors.size(); j++) {
			HE_Vertex* neighbor = neighbors[j];
			float weight = 0.0f;

			if (fairing != TAN && fairing != COT) {
				weight = GetWeight(temp, neighbor, fairing);
			}
			else {
				weight = GetWeightHarmonic(temp, neighbor, fairing);
			}

			weightSum += weight;
			step.x += weight * neighbor->Pos.x;
			step.y += weight * neighbor->Pos.y;
			step.z += weight * neighbor->Pos.z;
		}

		move.x += step.x / weightSum;
		move.y += step.y / weightSum;
		move.z += step.z / weightSum;

		temp->Pos.x += move.x;
		temp->Pos.y += move.y;
		temp->Pos.z += move.z;
	}
}

float HE_MeshData::GetWeight(HE_Vertex* vertex, HE_Vertex* neighbor, enum FMETHOD fairing) {
	switch (fairing){
	case UNIFORM:
		return 1.0f;
	case SCALE: {
		DirectX::XMFLOAT3 temp = DirectX::XMFLOAT3(
			vertex->Pos.x - neighbor->Pos.x, 
			vertex->Pos.y - neighbor->Pos.y, 
			vertex->Pos.z - neighbor->Pos.z
		);

		float ans = sqrt(temp.x * temp.x + temp.y * temp.y + temp.z * temp.z);
		return ans;
	}
	default:
		return 1.0f;
	}
}

float HE_MeshData::GetWeightHarmonic(HE_Vertex* vertex, HE_Vertex* neighbor, enum FMETHOD fairing) {
	float a = 0.0f, b = 0.0f;

	HE_Edge* edge = HE_Edges.at(std::make_pair(vertex->Index, neighbor->Index));

	for (int i = 0; i < 2; i++) {
		HE_Triangle* triangle = edge->Triangle;
		std::vector<std::int32_t> indices;

		indices.push_back(triangle->VertexIndex0);
		indices.push_back(triangle->VertexIndex1);
		indices.push_back(triangle->VertexIndex2);

		for (int j = 0; j < 3; j++) {
			HE_Vertex* temp = HE_Vertexes[indices[j]];

			if (temp != vertex && temp != neighbor) {

				DirectX::XMFLOAT3 diff0 = DirectX::XMFLOAT3(temp->Pos.x - vertex->Pos.x, temp->Pos.y - vertex->Pos.y, temp->Pos.z - vertex->Pos.z);
				DirectX::XMFLOAT3 diff1 = DirectX::XMFLOAT3(neighbor->Pos.x - vertex->Pos.x, neighbor->Pos.y - vertex->Pos.y, neighbor->Pos.z - vertex->Pos.z);
				DirectX::XMFLOAT3 diff2 = DirectX::XMFLOAT3(neighbor->Pos.x - temp->Pos.x, neighbor->Pos.y - temp->Pos.y, neighbor->Pos.z - temp->Pos.z);
				DirectX::XMFLOAT3 diff3 = DirectX::XMFLOAT3(vertex->Pos.x - temp->Pos.x, vertex->Pos.y - temp->Pos.y, vertex->Pos.z - temp->Pos.z);

				if (fairing == TAN) {
					if (i == 0) { a = tan(MathHelper::AngleBetweenVectors(diff0, diff1) / 2); }
					else		{ b = tan(MathHelper::AngleBetweenVectors(diff0, diff1) / 2); }
				}
				else if (fairing == COT) {
					if (i == 0) { a = 1 / tan(MathHelper::AngleBetweenVectors(diff3, diff2)); }
					else		{ b = 1 / tan(MathHelper::AngleBetweenVectors(diff3, diff2)); }
				}
				break;
			}
		}

		edge = edge->Pair;
	}

	DirectX::XMFLOAT3 diff = DirectX::XMFLOAT3(vertex->Pos.x - neighbor->Pos.x, vertex->Pos.y - neighbor->Pos.y, vertex->Pos.z - neighbor->Pos.z);
	float diff_len = sqrt(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z);

	if (fairing == COT) {
		return (a + b);
	}
	else if (fairing == TAN) {
		return (a + b) / diff_len;
	}

	return 0.0f;
}

void HE_MeshData::ReconstructTopology() {
	std::vector<std::vector<std::int32_t>> indices;
	for (UINT i = 0; i < HE_Triangles.size(); i++){
		std::vector<std::int32_t> temp;
		temp.push_back(HE_Triangles[i]->VertexIndex2);
		temp.push_back(HE_Triangles[i]->VertexIndex1);
		temp.push_back(HE_Triangles[i]->VertexIndex0);
		indices.push_back(temp);
	}
	HE_Triangles.clear();
	HE_Edges.clear();
	InsertTriangles(indices);
}



//Get Vertexes Normal==========================================================================================================================
void HE_MeshData::GetVertexesNormal() {
	for (std::int32_t i = 0; i < HE_Vertexes.size(); i++){
		GetVertexNormal(HE_Vertexes[i]);
	}
}

void HE_MeshData::GetVertexNormal(HE_Vertex* vertex) {
	std::vector<HE_Triangle*> triangles = GetTrianglesFromVertex(vertex);
	DirectX::XMFLOAT3 normal = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);

	for (int i = 0; i < triangles.size(); i++){
		normal.x += triangles[i]->Normal.x;
		normal.y += triangles[i]->Normal.y;
		normal.z += triangles[i]->Normal.z;
	}

	normal = MathHelper::Normalize(normal);
	vertex->Normal = normal;
}

