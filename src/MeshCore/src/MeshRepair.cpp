#include "MeshCore/HalfEdgeMesh.h"
#include "MeshMath.h"

#include <Eigen/Eigen>
#include <algorithm>
#include <cfloat>
#include <climits>
#include <cmath>
#include <unordered_set>

//Repair Normal==========================================================================================================================
void HE_MeshData::RepairNormal() {
	std::uint32_t correctFacetIndex = 0;
	bool allWrong = true;

	//找到一个正确面
	for (std::uint32_t i = 0; i < HE_Triangles.size(); i++) {
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

	Vec3f origin = Vec3f(x_origin, y_origin, z_origin);
	Vec3f dir = triangle->Normal;

	for (std::uint32_t i = 0; i < HE_Triangles.size(); i++) {
		HE_Triangle* temp = HE_Triangles[i];

		if (temp != triangle) {
			Vec3f v0 = HE_Vertexes[temp->VertexIndex0]->Pos;
			Vec3f v1 = HE_Vertexes[temp->VertexIndex1]->Pos;
			Vec3f v2 = HE_Vertexes[temp->VertexIndex2]->Pos;

			Vec3f S = SubVector(origin, v0);
			Vec3f E1 = SubVector(v1, v0);
			Vec3f E2 = SubVector(v2, v0);

			Vec3f S1 = CrossVector(dir, E2);
			Vec3f S2 = CrossVector(S, E1);

			float S1E1 = DotVector(S1, E1);
			if (std::fabs(S1E1) <= FLT_EPSILON) {
				continue;
			}
			float t = DotVector(S2, E2) / S1E1;
			float b1 = DotVector(S1, S) / S1E1;
			float b2 = DotVector(S2, dir) / S1E1;

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
	wrongTriangle->Normal = CalTriNormal(
		HE_Vertexes[wrongTriangle->VertexIndex0]->Pos,
		HE_Vertexes[wrongTriangle->VertexIndex1]->Pos,
		HE_Vertexes[wrongTriangle->VertexIndex2]->Pos
	);

	std::vector<std::uint32_t> vertexIndices;
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
	std::vector<Vec3f> expect_normals;

	for (std::uint32_t i = 0; i < HE_Triangles.size(); i++){
		std::vector<HE_Triangle*> neighbors = GetTrianglesFromTriangle(HE_Triangles[i]);
		Vec3f normal_expect = Vec3f(0.0f, 0.0f, 0.0f);

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
			HE_Triangle* neighbor = neighbors[j];
			if (neighbor == nullptr) {
				continue;
			}
			float temp = DotVector(HE_Triangles[i]->Normal, neighbor->Normal);
			float weight = temp > T ? pow(temp - T, 2) : 0;

			normal_expect.x += weight * neighbor->Normal.x;
			normal_expect.y += weight * neighbor->Normal.y;
			normal_expect.z += weight * neighbor->Normal.z;
		}

		expect_normals.push_back(normal_expect);
	}

	for (std::int32_t i = 0; i < HE_Vertexes.size(); i++){
		std::vector<HE_Triangle*> neighbors = GetTrianglesFromVertex(HE_Vertexes[i]);
		if (neighbors.empty()) {
			continue;
		}
		Vec3f move = Vec3f(0.0f, 0.0f, 0.0f);
		std::size_t validNeighbors = 0;

		for (int j = 0; j < neighbors.size(); j++){
			HE_Triangle* neighbor = neighbors[j];
			if (neighbor == nullptr) {
				continue;
			}
			if (neighbor->Index < 0 || static_cast<std::size_t>(neighbor->Index) >= expect_normals.size()) {
				continue;
			}
			Vec3f centroid = Vec3f(
				(HE_Vertexes[neighbor->VertexIndex0]->Pos.x + HE_Vertexes[neighbor->VertexIndex1]->Pos.x + HE_Vertexes[neighbor->VertexIndex2]->Pos.x) / 3,
				(HE_Vertexes[neighbor->VertexIndex0]->Pos.y + HE_Vertexes[neighbor->VertexIndex1]->Pos.y + HE_Vertexes[neighbor->VertexIndex2]->Pos.y) / 3,
				(HE_Vertexes[neighbor->VertexIndex0]->Pos.z + HE_Vertexes[neighbor->VertexIndex1]->Pos.z + HE_Vertexes[neighbor->VertexIndex2]->Pos.z) / 3
			);

			Vec3f diff = Vec3f(
				centroid.x - HE_Vertexes[i]->Pos.x, centroid.y - HE_Vertexes[i]->Pos.y, centroid.z - HE_Vertexes[i]->Pos.z
			);

			float weight = DotVector(expect_normals[neighbor->Index], diff);

			Vec3f step = Vec3f(
				weight * expect_normals[neighbor->Index].x, weight * expect_normals[neighbor->Index].y, weight * expect_normals[neighbor->Index].z
			);

			move.x += step.x; move.y += step.y; move.z += step.z;
			validNeighbors++;
		}

		if (validNeighbors == 0) {
			continue;
		}
		move.x /= static_cast<float>(validNeighbors);
		move.y /= static_cast<float>(validNeighbors);
		move.z /= static_cast<float>(validNeighbors);
		HE_Vertexes[i]->Pos.x = HE_Vertexes[i]->Pos.x + move.x;
		HE_Vertexes[i]->Pos.y = HE_Vertexes[i]->Pos.y + move.y;
		HE_Vertexes[i]->Pos.z = HE_Vertexes[i]->Pos.z + move.z;
	}

	for (std::uint32_t i = 0; i < HE_Triangles.size(); i++) {
		Vec3f correctNormal = CalTriNormal(
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

	for (std::uint32_t i = 0; i < HE_Triangles.size(); i++) {
		Vec3f correctNormal = CalTriNormal(
			HE_Vertexes[HE_Triangles[i]->VertexIndex0]->Pos,
			HE_Vertexes[HE_Triangles[i]->VertexIndex1]->Pos,
			HE_Vertexes[HE_Triangles[i]->VertexIndex2]->Pos
		);
		HE_Triangles[i]->Normal = correctNormal;
	}
}



//Holes Filling==========================================================================================================================
void HE_MeshData::HolesFilling(METHOD method, RMETHOD refinement, FMETHOD fairing, int fairingNum) {
	BoundaryLoops.clear();
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
				std::reverse(boundaryLoop.begin(), boundaryLoop.end());
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

	const std::int32_t n = static_cast<std::int32_t>(boundaryLoop.size());
	if (n < 3) {
		return;
	}
	{
		std::unordered_set<std::int32_t> uniqueBoundaryVertices;
		uniqueBoundaryVertices.reserve(boundaryLoop.size());
		for (std::int32_t vertexIndex : boundaryLoop) {
			if (vertexIndex < 0 || static_cast<std::size_t>(vertexIndex) >= HE_Vertexes.size()) {
				return;
			}
			if (!uniqueBoundaryVertices.insert(vertexIndex).second) {
				return;
			}
		}

		for (std::int32_t i = 0; i < n; i++) {
			const std::int32_t v0 = boundaryLoop[i];
			const std::int32_t v1 = boundaryLoop[(i + 1) % n];

			const auto hasBoundaryHalfEdge = [&](std::int32_t start, std::int32_t end) -> bool {
				auto edgeIt = HE_Edges.find(std::make_pair(start, end));
				return edgeIt != HE_Edges.end() && edgeIt->second != nullptr && edgeIt->second->Triangle == nullptr;
			};

			if (!hasBoundaryHalfEdge(v0, v1) && !hasBoundaryHalfEdge(v1, v0)) {
				return;
			}
		}
	}

	for (int i = n - 1; i > 0; i--){
		std::vector<float> vec(i);
		A.push_back(vec);

		if (i >= n - 2){
			std::vector<std::int32_t> v;
			ls.push_back(v);
		}
		else{
			std::vector<std::int32_t> v(i);
			ls.push_back(v);
		}
	}

	// Step 1 in the paper: initialize base triangle areas.
	if (A.size() > 1) {
		for (std::int32_t i = 0; i < n - 2; i++) {
			Vec3f v0 = HE_Vertexes[boundaryLoop[i]]->Pos;
			Vec3f v1 = HE_Vertexes[boundaryLoop[i + 1]]->Pos;
			Vec3f v2 = HE_Vertexes[boundaryLoop[i + 2]]->Pos;
			A[1][i] = GetTriArea(v0, v1, v2);
		}
	}

	//Triangulation
	if (method == ANGLE) {
		struct AngleWeight {
			float MaxDihedral = 0.0f;
			float Area = 0.0f;
		};

		constexpr float kAngleEps = 1e-6f;
		constexpr float kPi = 3.14159265358979323846f;
		const auto getBoundaryPos = [&](std::int32_t localIndex) -> const Vec3f& {
			return HE_Vertexes[boundaryLoop[localIndex]]->Pos;
		};
		const auto triAreaFromLocal = [&](std::int32_t i0, std::int32_t i1, std::int32_t i2) -> float {
			return GetTriArea(getBoundaryPos(i0), getBoundaryPos(i1), getBoundaryPos(i2));
		};
		const auto triangleOppositePos = [&](const HE_Triangle* tri, std::int32_t shared0, std::int32_t shared1, Vec3f& posOut) -> bool {
			if (tri == nullptr) {
				return false;
			}
			if (tri->VertexIndex0 < 0 || tri->VertexIndex1 < 0 || tri->VertexIndex2 < 0) {
				return false;
			}
			const std::size_t v0 = static_cast<std::size_t>(tri->VertexIndex0);
			const std::size_t v1 = static_cast<std::size_t>(tri->VertexIndex1);
			const std::size_t v2 = static_cast<std::size_t>(tri->VertexIndex2);
			if (v0 >= HE_Vertexes.size() || v1 >= HE_Vertexes.size() || v2 >= HE_Vertexes.size()) {
				return false;
			}

			const std::int32_t vertices[3] = { tri->VertexIndex0, tri->VertexIndex1, tri->VertexIndex2 };
			bool hasShared0 = false;
			bool hasShared1 = false;
			std::int32_t oppositeVertex = -1;

			for (std::int32_t vertexIndex : vertices) {
				if (vertexIndex == shared0) {
					hasShared0 = true;
				}
				else if (vertexIndex == shared1) {
					hasShared1 = true;
				}
				else {
					oppositeVertex = vertexIndex;
				}
			}

			if (!hasShared0 || !hasShared1 || oppositeVertex < 0) {
				return false;
			}

			posOut = HE_Vertexes[oppositeVertex]->Pos;
			return true;
		};
		const auto boundaryAdjacentOppositePos = [&](std::int32_t i0, std::int32_t i1, Vec3f& posOut) -> bool {
			const std::int32_t vStart = boundaryLoop[i0];
			const std::int32_t vEnd = boundaryLoop[i1];

			const auto tryEdge = [&](std::int32_t a, std::int32_t b) -> bool {
				auto edgeIt = HE_Edges.find(std::make_pair(a, b));
				if (edgeIt == HE_Edges.end() || edgeIt->second == nullptr) {
					return false;
				}
				HE_Edge* edge = edgeIt->second;
				if (triangleOppositePos(edge->Triangle, a, b, posOut)) {
					return true;
				}
				if (edge->Pair != nullptr && triangleOppositePos(edge->Pair->Triangle, a, b, posOut)) {
					return true;
				}
				return false;
			};

			if (tryEdge(vStart, vEnd)) {
				return true;
			}
			return tryEdge(vEnd, vStart);
		};
		const auto dpAdjacentOppositeVertex = [&](std::int32_t i0, std::int32_t i1, std::int32_t& oppositeLocal) -> bool {
			const std::int32_t d = i1 - i0;
			if (d <= 1) {
				return false;
			}

			std::int32_t split = i0 + 1;
			if (d > 2) {
				split = ls[d - 1][i0];
			}
			if (split <= i0 || split >= i1) {
				return false;
			}
			oppositeLocal = split;
			return true;
		};
		// Compare the two face planes around their shared edge instead of using stored face winding.
		const auto edgeDihedral = [&](std::int32_t edgeLocal0, std::int32_t edgeLocal1, const Vec3f& opposite0, const Vec3f& opposite1) -> float {
			const Vec3f edgeStart = getBoundaryPos(edgeLocal0);
			const Vec3f edgeEnd = getBoundaryPos(edgeLocal1);
			const Vec3f edgeVector = SubVector(edgeEnd, edgeStart);
			const float edgeLength = Length(edgeVector);
			if (edgeLength <= FLT_EPSILON) {
				return 0.0f;
			}

			const Vec3f edgeUnit = Vec3f(edgeVector.x / edgeLength, edgeVector.y / edgeLength, edgeVector.z / edgeLength);
			const auto projectAroundEdge = [&](const Vec3f& point) -> Vec3f {
				const Vec3f offset = SubVector(point, edgeStart);
				const float alongEdge = DotVector(offset, edgeUnit);
				return Vec3f(
					offset.x - alongEdge * edgeUnit.x,
					offset.y - alongEdge * edgeUnit.y,
					offset.z - alongEdge * edgeUnit.z
				);
			};

			const Vec3f radial0 = projectAroundEdge(opposite0);
			const Vec3f radial1 = projectAroundEdge(opposite1);
			const float radialLength0 = Length(radial0);
			const float radialLength1 = Length(radial1);
			if (radialLength0 <= FLT_EPSILON || radialLength1 <= FLT_EPSILON) {
				return 0.0f;
			}

			float cosine = DotVector(radial0, radial1) / (radialLength0 * radialLength1);
			cosine = std::max(-1.0f, std::min(1.0f, cosine));
			return kPi - std::acos(cosine);
		};
		const auto betterWeight = [&](const AngleWeight& lhs, const AngleWeight& rhs) -> bool {
			if (lhs.MaxDihedral + kAngleEps < rhs.MaxDihedral) {
				return true;
			}
			if (rhs.MaxDihedral + kAngleEps < lhs.MaxDihedral) {
				return false;
			}
			return lhs.Area + kAngleEps < rhs.Area;
		};

		std::vector<std::vector<AngleWeight>> W;
		W.reserve(A.size());
		for (const auto& row : A) {
			W.emplace_back(row.size());
		}

		// Step 1: initialize W(i, i+1) = (0,0), and W(i, i+2) = Omega(i,i+1,i+2).
		if (W.size() > 1) {
			for (std::int32_t i = 0; i < n - 2; i++) {
				float mu = 0.0f;
				Vec3f adjacentOpposite;
				if (boundaryAdjacentOppositePos(i, i + 1, adjacentOpposite)) {
					mu = std::max(mu, edgeDihedral(i, i + 1, getBoundaryPos(i + 2), adjacentOpposite));
				}
				if (boundaryAdjacentOppositePos(i + 1, i + 2, adjacentOpposite)) {
					mu = std::max(mu, edgeDihedral(i + 1, i + 2, getBoundaryPos(i), adjacentOpposite));
				}

				W[1][i].MaxDihedral = mu;
				W[1][i].Area = triAreaFromLocal(i, i + 1, i + 2);
			}
		}

		// Step 2: dynamic programming over increasing subpolygon size.
		for (std::int32_t d = 3; d < n; d++) {
			for (std::int32_t i = 0; i < n - d; i++) {
				const std::int32_t k = i + d;
				AngleWeight best;
				best.MaxDihedral = FLT_MAX;
				best.Area = FLT_MAX;
				std::int32_t bestSplit = i + 1;

				for (std::int32_t split = 0; split < d - 1; split++) {
					const std::int32_t m = i + split + 1;

					float mu = 0.0f;
					Vec3f adjacentOpposite;
					std::int32_t adjacentOppositeLocal = -1;

					if (m - i > 1) {
						if (dpAdjacentOppositeVertex(i, m, adjacentOppositeLocal)) {
							mu = std::max(mu, edgeDihedral(i, m, getBoundaryPos(k), getBoundaryPos(adjacentOppositeLocal)));
						}
					}
					else if (boundaryAdjacentOppositePos(i, m, adjacentOpposite)) {
						mu = std::max(mu, edgeDihedral(i, m, getBoundaryPos(k), adjacentOpposite));
					}

					if (k - m > 1) {
						if (dpAdjacentOppositeVertex(m, k, adjacentOppositeLocal)) {
							mu = std::max(mu, edgeDihedral(m, k, getBoundaryPos(i), getBoundaryPos(adjacentOppositeLocal)));
						}
					}
					else if (boundaryAdjacentOppositePos(m, k, adjacentOpposite)) {
						mu = std::max(mu, edgeDihedral(m, k, getBoundaryPos(i), adjacentOpposite));
					}

					if (i == 0 && k == n - 1 && boundaryAdjacentOppositePos(i, k, adjacentOpposite)) {
						mu = std::max(mu, edgeDihedral(i, k, getBoundaryPos(m), adjacentOpposite));
					}

					const AngleWeight& left = W[m - i - 1][i];
					const AngleWeight& right = W[k - m - 1][m];

					AngleWeight candidate;
					candidate.MaxDihedral = std::max(mu, std::max(left.MaxDihedral, right.MaxDihedral));
					candidate.Area = left.Area + right.Area + triAreaFromLocal(i, m, k);

					if (betterWeight(candidate, best)) {
						best = candidate;
						bestSplit = m;
					}
				}

				ls[d - 1][i] = bestSplit;
				W[d - 1][i] = best;
				A[d - 1][i] = best.Area;
			}
		}
	}
	else if(method == AREA) {
		for (std::int32_t i = 3; i < n; i++){
			for (std::int32_t j = 0; j < n - i; j++){

				float minArea = FLT_MAX;
				std::int32_t KoPt = INT_MIN;

				for (int k = 0; k < i - 1; k++){
					Vec3f v0 = HE_Vertexes[boundaryLoop[j]]->Pos;
					Vec3f v1 = HE_Vertexes[boundaryLoop[k + j + 1]]->Pos;
					Vec3f v2 = HE_Vertexes[boundaryLoop[j + i]]->Pos;

					float area = GetTriArea(v0, v1, v2) + A[k][j] + A[i - k - 2][k + j + 1];
					
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

	for (std::uint32_t i = 0; i < HE_Triangles.size(); i++) {
		Vec3f correctNormal = CalTriNormal(
			HE_Vertexes[HE_Triangles[i]->VertexIndex0]->Pos,
			HE_Vertexes[HE_Triangles[i]->VertexIndex1]->Pos,
			HE_Vertexes[HE_Triangles[i]->VertexIndex2]->Pos
		);
		HE_Triangles[i]->Normal = correctNormal;
	}
}


void HE_MeshData::Refinement(std::vector<std::int32_t>& boundaryLoop, std::vector<std::vector<std::int32_t>>& ls, std::vector<std::vector<std::int32_t>>& triangles, RMETHOD refinement) {
	std::vector<Vec3f> centroids;
	
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

void HE_MeshData::TrianglesToBeInserted(std::vector<std::int32_t>& boundaryLoop, std::vector<std::vector<std::int32_t>>& ls, std::vector<Vec3f>& centroids, std::vector<std::vector<std::int32_t>>& triangles) {
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

		centroids.push_back(Vec3f(x, y, z));

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
		if (neighbors.empty()) {
			continue;
		}
		std::size_t validNeighbors = 0;

		for (int j = 0; j < neighbors.size(); j++){
			HE_Vertex* adj = neighbors[j];
			if (adj == nullptr) {
				continue;
			}
			sigmas[boundaryLoop[i]] += sqrt(pow(temp->Pos.x - adj->Pos.x, 2) + pow(temp->Pos.y - adj->Pos.y, 2) + pow(temp->Pos.z - adj->Pos.z, 2));
			validNeighbors++;
		}

		if (validNeighbors == 0) {
			sigmas[boundaryLoop[i]] = 0.0f;
			continue;
		}
		sigmas[boundaryLoop[i]] /= static_cast<float>(validNeighbors);
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


void HE_MeshData::IdentifyDividedTriangles(std::vector<std::vector<std::int32_t>>& triangles, std::vector<Vec3f>& centroids, std::unordered_map<std::int32_t, float>& sigmas, std::vector<float>& centroidSigmas, std::vector<std::int32_t>& trianglesToBeDivided) {
	for (int i = 0; i < triangles.size(); i++){
		float sigma_vertex_centroid = centroidSigmas[i];
		Vec3f vertex_centroid = centroids[i];
		
		for (int j = 0; j < 3; j++){
			Vec3f vertex = HE_Vertexes[triangles[i][j]]->Pos;

			Vec3f diff = Vec3f(vertex_centroid.x - vertex.x, vertex_centroid.y - vertex.y, vertex_centroid.z - vertex.z);
			float diff_len = sqrt(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z);

			float sigma_vertex = sigmas[triangles[i][j]];

			if (sqrt(2) * diff_len > sigma_vertex_centroid && sqrt(2) * diff_len > sigma_vertex){
				trianglesToBeDivided.push_back(i);
				break;
			}
		}
	}
}


void HE_MeshData::DivideTriangles(std::vector<std::vector<std::int32_t>>& triangles, std::vector<std::int32_t>& trianglesToBeDivided, std::vector<Vec3f>& centroids, std::unordered_map<std::int32_t, float>& sigmas, std::vector<float>& centroidSigmas, std::vector<std::pair<std::int32_t, std::int32_t>>& edgesToBeRelaxed) {
	for (int i = 0; i < trianglesToBeDivided.size(); i++){
		Vec3f coord = centroids[trianglesToBeDivided[i]];

		HE_Vertex* centroid = new HE_Vertex();
		centroid->Index = HE_Vertexes.size();
		centroid->Pos = coord;
		centroid->Normal = Vec3f(0.0f, 0.0f, 0.0f);
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

		Vec3f centroid0 = Vec3f(
			(HE_Vertexes[tri0[0]]->Pos.x + HE_Vertexes[tri0[1]]->Pos.x + HE_Vertexes[tri0[2]]->Pos.x) / 3.0f,
			(HE_Vertexes[tri0[0]]->Pos.y + HE_Vertexes[tri0[1]]->Pos.y + HE_Vertexes[tri0[2]]->Pos.y) / 3.0f,
			(HE_Vertexes[tri0[0]]->Pos.z + HE_Vertexes[tri0[1]]->Pos.z + HE_Vertexes[tri0[2]]->Pos.z) / 3.0f
		);
		Vec3f centroid1 = Vec3f(
			(HE_Vertexes[tri1[0]]->Pos.x + HE_Vertexes[tri1[1]]->Pos.x + HE_Vertexes[tri1[2]]->Pos.x) / 3.0f,
			(HE_Vertexes[tri1[0]]->Pos.y + HE_Vertexes[tri1[1]]->Pos.y + HE_Vertexes[tri1[2]]->Pos.y) / 3.0f,
			(HE_Vertexes[tri1[0]]->Pos.z + HE_Vertexes[tri1[1]]->Pos.z + HE_Vertexes[tri1[2]]->Pos.z) / 3.0f
		);
		Vec3f centroid2 = Vec3f(
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


void HE_MeshData::RelaxEdge(std::vector<std::vector<std::int32_t>>& triangles, std::int32_t v0, std::int32_t v1, std::vector<Vec3f>& centroids, std::unordered_map<std::int32_t, float>& sigmas, std::vector<float>& centroidSigmas) {
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
			
			centroids[tris[0]] = Vec3f(
				(HE_Vertexes[op0]->Pos.x + HE_Vertexes[op1]->Pos.x + HE_Vertexes[v0]->Pos.x) / 3.0f,
				(HE_Vertexes[op0]->Pos.y + HE_Vertexes[op1]->Pos.y + HE_Vertexes[v0]->Pos.y) / 3.0f,
				(HE_Vertexes[op0]->Pos.z + HE_Vertexes[op1]->Pos.z + HE_Vertexes[v0]->Pos.z) / 3.0f
			);

			centroids[tris[1]] = Vec3f(
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


void HE_MeshData::Relax(std::vector<std::vector<std::int32_t>>& triangles, std::int32_t& numOfSwap, std::int32_t& sameCounter, std::vector<Vec3f>& centroids, std::unordered_map<std::int32_t, float>& sigmas, std::vector<float>& centroidSigmas) {
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

void HE_MeshData::Swap(std::vector<std::vector<std::int32_t>>& triangles, std::int32_t tri0, std::int32_t op0, std::int32_t tri1, std::int32_t op1, std::int32_t iv0, std::int32_t iv1, std::vector<Vec3f>& centroids, std::unordered_map<std::int32_t, float>& sigmas, std::vector<float>& centroidSigmas) {
	if (SwapCheck(triangles, iv0, iv1, op0, op1)){
		triangles[tri0][0] = iv0;
		triangles[tri0][1] = op1;
		triangles[tri0][2] = op0;

		triangles[tri1][0] = iv1;
		triangles[tri1][1] = op0;
		triangles[tri1][2] = op1;

		centroids[tri0] = Vec3f(
			(HE_Vertexes[op0]->Pos.x + HE_Vertexes[op1]->Pos.x + HE_Vertexes[iv0]->Pos.x) / 3.0f,
			(HE_Vertexes[op0]->Pos.y + HE_Vertexes[op1]->Pos.y + HE_Vertexes[iv0]->Pos.y) / 3.0f,
			(HE_Vertexes[op0]->Pos.z + HE_Vertexes[op1]->Pos.z + HE_Vertexes[iv0]->Pos.z) / 3.0f
		);

		centroids[tri1] = Vec3f(
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
		Vec3f correctNormal = CalTriNormal(
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
		if (neighbors.empty()) {
			continue;
		}

		float weightSum = 0.0f;
		Vec3f step = Vec3f(0.0f, 0.0f, 0.0f);
		Vec3f move = Vec3f(-temp->Pos.x, -temp->Pos.y, -temp->Pos.z);
		
		for (int j = 0; j < neighbors.size(); j++) {
			HE_Vertex* neighbor = neighbors[j];
			if (neighbor == nullptr) {
				continue;
			}
			float weight = 0.0f;

			if (fairing != TAN && fairing != COT) {
				weight = GetWeight(temp, neighbor, fairing);
			}
			else {
				weight = GetWeightHarmonic(temp, neighbor, fairing);
			}
			if (!std::isfinite(weight)) {
				continue;
			}

			weightSum += weight;
			step.x += weight * neighbor->Pos.x;
			step.y += weight * neighbor->Pos.y;
			step.z += weight * neighbor->Pos.z;
		}
		if (std::fabs(weightSum) <= FLT_EPSILON) {
			continue;
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
		Vec3f temp = Vec3f(
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
	auto edgeIt = HE_Edges.find(std::make_pair(vertex->Index, neighbor->Index));
	if (edgeIt == HE_Edges.end() || edgeIt->second == nullptr) {
		return 0.0f;
	}

	float weightSum = 0.0f;
	HE_Edge* baseEdge = edgeIt->second;

	for (int i = 0; i < 2; i++) {
		HE_Edge* currentEdge = (i == 0) ? baseEdge : baseEdge->Pair;
		if (currentEdge == nullptr || currentEdge->Triangle == nullptr) {
			continue;
		}

		HE_Triangle* triangle = currentEdge->Triangle;
		std::vector<std::int32_t> indices;
		indices.push_back(triangle->VertexIndex0);
		indices.push_back(triangle->VertexIndex1);
		indices.push_back(triangle->VertexIndex2);

		for (int j = 0; j < 3; j++) {
			HE_Vertex* temp = HE_Vertexes[indices[j]];
			if (temp == nullptr || temp == vertex || temp == neighbor) {
				continue;
			}

			Vec3f diff0 = Vec3f(temp->Pos.x - vertex->Pos.x, temp->Pos.y - vertex->Pos.y, temp->Pos.z - vertex->Pos.z);
			Vec3f diff1 = Vec3f(neighbor->Pos.x - vertex->Pos.x, neighbor->Pos.y - vertex->Pos.y, neighbor->Pos.z - vertex->Pos.z);
			Vec3f diff2 = Vec3f(neighbor->Pos.x - temp->Pos.x, neighbor->Pos.y - temp->Pos.y, neighbor->Pos.z - temp->Pos.z);
			Vec3f diff3 = Vec3f(vertex->Pos.x - temp->Pos.x, vertex->Pos.y - temp->Pos.y, vertex->Pos.z - temp->Pos.z);

			if (fairing == TAN) {
				float contribution = tan(AngleBetweenVectors(diff0, diff1) / 2.0f);
				if (std::isfinite(contribution)) {
					weightSum += contribution;
				}
			}
			else if (fairing == COT) {
				float tanValue = tan(AngleBetweenVectors(diff3, diff2));
				if (std::fabs(tanValue) > FLT_EPSILON) {
					float contribution = 1.0f / tanValue;
					if (std::isfinite(contribution)) {
						weightSum += contribution;
					}
				}
			}
			break;
		}
	}

	if (fairing == COT) {
		return weightSum;
	}
	if (fairing == TAN) {
		Vec3f diff = Vec3f(vertex->Pos.x - neighbor->Pos.x, vertex->Pos.y - neighbor->Pos.y, vertex->Pos.z - neighbor->Pos.z);
		float diff_len = sqrt(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z);
		if (diff_len <= FLT_EPSILON) {
			return 0.0f;
		}
		return weightSum / diff_len;
	}

	return 0.0f;
}
