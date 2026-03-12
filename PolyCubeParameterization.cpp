#include "PolyCubeParameterization.h"

PolyCubeParameterization::~PolyCubeParameterization() {

}

void PolyCubeParameterization::Parameterize() {

	Axes.push_back(Vector3f(1, 0, 0));		Axes.push_back(Vector3f(-1, 0, 0));
	Axes.push_back(Vector3f(0, 1, 0));		Axes.push_back(Vector3f(0, -1, 0));
	Axes.push_back(Vector3f(0, 0, 1));		Axes.push_back(Vector3f(0, 0, -1));

	GetFacetsArea();
	DyeVertexes();
	RotateVertexes(0);
	LoadFacetsSeeds("D:/Projects/Project1/models/", "CorrectTorusNomal");
	SetSurfacesType();
	//SetSurfacesCoord();
	//Smoothing(1, 0.25);
	//ComputeDistortion(Isometric);
}

void PolyCubeParameterization::GetFacetsArea() {
	for (int i = 0; i < stl->LoadedHE_Facets.size(); i++){
		stl->LoadedHE_Facets[i]->area = GetFacetArea(i);
	}
}
float PolyCubeParameterization::GetFacetArea(int i) {
	HE_Point* v0 = stl->LoadedHE_Points[stl->LoadedHE_Facets[i]->PointIndices[0]];
	HE_Point* v1 = stl->LoadedHE_Points[stl->LoadedHE_Facets[i]->PointIndices[1]];
	HE_Point* v2 = stl->LoadedHE_Points[stl->LoadedHE_Facets[i]->PointIndices[2]];

	Vector3f e0 = v1->coord - v0->coordPara;
	Vector3f e1 = v2->coord - v0->coordPara;

	Vector3f avec = e0.cross(e1);

	return avec.norm() / 2;
}

void PolyCubeParameterization::Smoothing(int n, float step) {
	for (int k = 0; k < n; k++){
		for (int i = 0; i < stl->LoadedHE_Points.size(); i++) {
			vector<HE_Point*> temp = He_Mesh->GetVertexsFromVertex(stl->LoadedHE_Points[i]);
			Vector3f deltaPoint = Vector3f(0.0f, 0.0f, 0.0f);
			vector<float> weights;
			float weightSum = 0;

			//计算该顶点相对于一阶邻域的拉普拉斯权重
			for (int j = 0; j < temp.size(); j++) {
				weights.push_back(GetWeight(stl->LoadedHE_Points[i], temp[j], Cot));
				weightSum += weights[j];
			}

			//deltaPoint = ∑(weight * 邻域点坐标) - 该点坐标
			for (int j = 0; j < temp.size(); j++) {
				float weight = weights[j] / weightSum;
				deltaPoint += (weight * temp[j]->coordPara);
			}
			deltaPoint -= stl->LoadedHE_Points[i]->coordPara;

			stl->LoadedHE_Points[i]->coordPara += step * deltaPoint;
		}
	}
}
float PolyCubeParameterization::GetWeight(HE_Point* point, HE_Point* neighbor, weightType type) {
	//均匀拉普拉斯
	if (type == Uniform) {
		float ans = (point->coordPara - neighbor->coordPara).norm();
		return ans;
	}

	else {
		//余切/正切拉普拉斯
		float ans = 0.0f, a = 0.0f, b = 0.0f;
		HE_Edge* edge = stl->He_Mesh.Edges.at(make_pair(point->index, neighbor->index));

		for (int i = 0; i < 2; i++) {
			HE_Facet* facet = edge->facet;

			for (int j = 0; j < facet->PointIndices.size(); j++) {
				HE_Point* temp = stl->LoadedHE_Points[facet->PointIndices[j]];
				if (temp != point && temp != neighbor) {

					if (type == Tan) {
						if (i == 0) { a = tan(AngleBetweenV3f((temp->coordPara - point->coordPara), (neighbor->coordPara - point->coordPara)) / 2); }
						else		{ b = tan(AngleBetweenV3f((temp->coordPara - point->coordPara), (neighbor->coordPara - point->coordPara)) / 2); }
					}
					else if (type == Cot) {
						if (i == 0) { a = 1 / tan(AngleBetweenV3f((point->coordPara - temp->coordPara), (neighbor->coord - temp->coordPara))); }
						else		{ b = 1 / tan(AngleBetweenV3f((point->coordPara - temp->coordPara), (neighbor->coord - temp->coordPara))); }
					}
					break;
				}
			}
			edge = edge->pair;
		}

		if (type == Cot) {
			return (a + b);
		}
		else if (type == Tan) {
			return (a + b) / (point->coord - neighbor->coord).norm();
		}
	}
}

void PolyCubeParameterization::DyeVertexes() {
	//重新计算一次法线，避免之前有更改
	//for (int i = 0; i < stl->LoadedHE_Facets.size(); i++){
	//	stl->LoadedHE_Facets[i]->normal = GetTriNormal(stl->LoadedHE_Points[stl->LoadedHE_Facets[i]->PointIndices[0]]->coordPara,
	//												   stl->LoadedHE_Points[stl->LoadedHE_Facets[i]->PointIndices[1]]->coordPara,
	//												   stl->LoadedHE_Points[stl->LoadedHE_Facets[i]->PointIndices[2]]->coordPara);
	//}

	//遍历每个点
	for (int i = 0; i < stl->LoadedHE_Points.size(); i++){
		HE_Point* point = stl->LoadedHE_Points[i];

		//遍历该点周围的所有面，计算法线平均值，作为该点的法线
		vector<HE_Facet*> facets = He_Mesh->GetFacetsFromVertex(point);
		Vector3f normal = Vector3f(0.0f, 0.0f, 0.0f);
		for (int j = 0; j < facets.size(); j++){
			normal += facets[j]->normal;
		}
		point->normal = normal / (facets.size());

		//染色(获取期望轴向)
		point->normalExpect = GetVertexColor(normal);
	}
}
Vector3f PolyCubeParameterization::GetVertexColor(Vector3f normal) {
	//计算法线相较于每个轴向的角度
	vector<float> angles; 
	for (int i = 0; i < Axes.size(); i++){
		angles.push_back(AngleBetweenV3f(normal, Axes[i]));
	}
	
	//角度最小的就是期望轴向
	int min = min_element(angles.begin(), angles.end()) - angles.begin();
	return Axes[min];
}

Vector3f PolyCubeParameterization::GetFacetColorFromType(int type) {
	switch (type){
	case 1:
		return Vector3f(1, 0, 0);
		break;
	case 2:
		return Vector3f(-1, 0, 0);
		break;
	case 3:
		return Vector3f(0, 1, 0);
		break;
	case 4:
		return Vector3f(0, -1, 0);
		break;
	case 5:
		return Vector3f(0, 0, 1);
		break;
	case 6:
		return Vector3f(0, 0, -1);
		break;
	}
}

void PolyCubeParameterization::RotateVertexes(int n){
	for (int m = 0; m < n; m++){
		MatrixXf A = MatrixXf::Zero(stl->LoadedHE_Points.size(), stl->LoadedHE_Points.size());
		MatrixXf B = MatrixXf::Zero(stl->LoadedHE_Points.size(), 3);
		Eigen::MatrixXf NewCoord = MatrixXf::Zero(stl->LoadedHE_Points.size(), 3);
		ComputeA(A);
		ComputeB(B);

		Eigen::SparseMatrix<float> A_sparse(stl->LoadedHE_Points.size(), stl->LoadedHE_Points.size());
		Eigen::SparseMatrix<float> B_sparse(stl->LoadedHE_Points.size(), 3);
		Eigen::LeastSquaresConjugateGradient<Eigen::SparseMatrix<float>> Solver_sparse;

		buildSparseMatrix(A_sparse, A, stl->LoadedHE_Points.size(), stl->LoadedHE_Points.size());
		buildSparseMatrix(B_sparse, B, stl->LoadedHE_Points.size(), 3);

		// 设置迭代精度
		Solver_sparse.setTolerance(0.0001f);
		Solver_sparse.compute(A_sparse);

		NewCoord = Solver_sparse.solve(B_sparse);

		//将newCoord赋给坐标
		for (int i = 0; i < stl->LoadedHE_Points.size(); i++) {
			if (stl->LoadedHE_Points[i]->surfaceType != 0) {
				stl->LoadedHE_Points[i]->coordPara.x() = NewCoord(i, 0);
				stl->LoadedHE_Points[i]->coordPara.y() = NewCoord(i, 1);
				stl->LoadedHE_Points[i]->coordPara.z() = NewCoord(i, 2);
			}
		}

		//重新计算每个点的法线
		for (int i = 0; i < stl->LoadedHE_Facets.size(); i++) {
			stl->LoadedHE_Facets[i]->normal = GetTriNormal(stl->LoadedHE_Points[stl->LoadedHE_Facets[i]->PointIndices[0]]->coordPara,
														   stl->LoadedHE_Points[stl->LoadedHE_Facets[i]->PointIndices[1]]->coordPara,
														   stl->LoadedHE_Points[stl->LoadedHE_Facets[i]->PointIndices[2]]->coordPara);
		}
		for (int i = 0; i < stl->LoadedHE_Points.size(); i++) {
			HE_Point* point = stl->LoadedHE_Points[i];

			//遍历该点周围的所有面，计算法线平均值，作为该点的法线
			vector<HE_Facet*> facets = He_Mesh->GetFacetsFromVertex(point);
			Vector3f normal = Vector3f(0.0f, 0.0f, 0.0f);
			for (int j = 0; j < facets.size(); j++) {
				normal += facets[j]->normal;
			}
			point->normal = normal / (facets.size());
		}
	}
}
void PolyCubeParameterization::ComputeA(Eigen::MatrixXf& A) {
	//构建delta坐标系
	for (int i = 0; i < stl->LoadedHE_Points.size(); i++) {
		//(i,i) = 1
		A(i, i) = 1;

		vector<HE_Point*> temp = He_Mesh->GetVertexsFromVertex(stl->LoadedHE_Points[i]);
		vector<float> weights;
		float weightSum = 0;

		//计算(i,j)的均匀/cot/tan权重，此处仅能使用均匀权重，其他出错
		for (int j = 0; j < temp.size(); j++) {
			weights.push_back(GetWeight(stl->LoadedHE_Points[i], temp[j], Uniform));
			weightSum += weights[j];
		}

		//(i,j) = -weight
		for (int j = 0; j < temp.size(); j++) {
			float weight = weights[j] / weightSum;
			A(i, temp[j]->index) = -weight;
		}
	}
}
void PolyCubeParameterization::ComputeB(Eigen::MatrixXf& B) {
	for (int i = 0; i < stl->LoadedHE_Points.size(); i++){
		HE_Point* point = stl->LoadedHE_Points[i];
		
		//计算点i的旋转矩阵
		MatrixXf rotateI = MatrixXf::Zero(3, 3);
		ComputeRotateMatrix(rotateI, *point);
		
		vector<HE_Point*> points = He_Mesh->GetVertexsFromVertex(point);
		MatrixXf ans = MatrixXf::Zero(3, 1);
		
		for (int j = 0; j < points.size(); j++){
			//计算点i的旋转矩阵
			MatrixXf rotateJ = MatrixXf::Zero(3, 3);
			ComputeRotateMatrix(rotateJ, *points[j]);

			//依照公式构建
			MatrixXf temp1 = MatrixXf::Zero(3, 3);
			temp1 = (rotateI + rotateJ) / 2;
			
			MatrixXf temp2 = MatrixXf::Zero(3, 1);
			temp2 = (point->coordPara - points[j]->coordPara);

			ans += temp1 * temp2;
		}

		//依照公式构建
		B(i, 0) = ans(0, 0) / points.size();
		B(i, 1) = ans(1, 0) / points.size();
		B(i, 2) = ans(2, 0) / points.size();
	}
}
void PolyCubeParameterization::ComputeRotateMatrix(Eigen::MatrixXf& matrix, HE_Point& point) {
	//获取旋转矩阵
	Vector3f axis = point.normal.cross(point.normalExpect);
	float angle = AngleBetweenV3f(point.normal, point.normalExpect);

	AngleAxis<float> rot(angle, axis);
	matrix = rot.toRotationMatrix();
	
}
void PolyCubeParameterization::buildSparseMatrix(Eigen::SparseMatrix<float>& A1_sparse, Eigen::MatrixXf A, int A_rows, int A_cols) {
	std::vector<Eigen::Triplet<float>> tripletlist;  //构建类型为三元组的vector

	for (int i = 0; i < A_rows; i++) {
		for (int j = 0; j < A_cols; j++) {
			if (std::fabs(A(i, j)) > 1e-6) {
				//按Triplet方式填充，速度快
				tripletlist.push_back(Eigen::Triplet<float>(i, j, A(i, j)));
			}
		}
	}
	A1_sparse.setFromTriplets(tripletlist.begin(), tripletlist.end());
	// 压缩优化矩阵
	A1_sparse.makeCompressed();
}

void PolyCubeParameterization::LoadFacetsSeeds(string file_path, string file_name) {
	file_path.append(file_name);
	file_path.append(".txt");

	fstream file1;
	file1.open(file_path, ios::in);

	file1.seekg(0, ios::end);
	unsigned int sizeofFile = file1.tellg();
	file1.seekg(0, ios::beg);

	char* buffer = (char*)malloc(sizeof(char) * sizeofFile);

	file1.read(buffer, sizeofFile);
	file1.close();

	stringstream ss(buffer);
	unsigned int index, type;

	//读取顶点
	do {
		ss >> type;
		if (type == 99999) {
			break;
		}
		ss >> index;
		stl->LoadedHE_Points[index]->surfaceType = type;
		vector<HE_Point*>* facet = new vector<HE_Point*>;
		facet->push_back(stl->LoadedHE_Points[index]);
		Surfaces.push_back(facet);
	} while (1);

	free(buffer);
}
void PolyCubeParameterization::SetSurfacesType() {
	for (int i = 0; i < Surfaces.size(); i++) {
		SetSufaceType(*Surfaces[i], i + 1);
	}
}
void PolyCubeParameterization::SetSufaceType(vector<HE_Point*>& surface, int facetType) {
	vector<HE_Point*> points = He_Mesh->GetVertexsFromVertex(surface[0]);
	queue<HE_Point*> qpoints;

	for (int i = 0; i < points.size(); i++) {
		qpoints.push(points[i]);
		points[i]->surfaceType = facetType;
	}

	for (vector<HE_Point*>::iterator it = points.begin(); it != points.end(); ++it) {
		if (*it != nullptr) {
			//delete[](*it);
			*it = nullptr;
		}
	}
	points.clear();
	points.shrink_to_fit();

	while (!qpoints.empty()) {
		HE_Point* temp = qpoints.front();
		surface.push_back(temp);
		qpoints.pop();

		if (temp->boundaryType < 0) {
			points = He_Mesh->GetVertexsFromVertex(temp);

			for (int i = 0; i < points.size(); i++) {
				if (points[i]->surfaceType < 0 && points[i]->normalExpect == surface[0]->normalExpect) {
					qpoints.push(points[i]);
					points[i]->surfaceType = facetType;
				}
				if (temp->normalExpect == surface[0]->normalExpect && points[i]->normalExpect != surface[0]->normalExpect) {
					qpoints.push(points[i]);
				}
			}

			for (vector<HE_Point*>::iterator it = points.begin(); it != points.end(); ++it) {
				if (*it != nullptr) {
					//delete[](*it);
					*it = nullptr;
				}
			}
			points.clear();
			points.shrink_to_fit();
		}
	}


}

void PolyCubeParameterization::SetSurfacesCoord() {
	for (int i = 0; i < Surfaces.size(); i++) {
		SetSurfaceCoord(*Surfaces[i]);
	}

}
void PolyCubeParameterization::SetSurfaceCoord(vector<HE_Point*>& surface) {
	if (surface[0]->normalExpect == Vector3f(1, 0, 0) || surface[0]->normalExpect == Vector3f(-1, 0, 0)) {
		float x = 0.0f;
		for (int i = 0; i < surface.size(); i++) {
			x += surface[i]->coordPara.x();
		}
		x /= surface.size();
		//x = (float)(int)x;
		//if (surface[0]->normalExpect == Vector3f(1, 0, 0)) {
		//	x = x + 1;
		//}
		//else {
		//	x = x - 1;
		//}
		for (int i = 0; i < surface.size(); i++) {
			surface[i]->coordPara.x() = x;
		}
	}
	else if (surface[0]->normalExpect == Vector3f(0, 1, 0) || surface[0]->normalExpect == Vector3f(0, -1, 0)) {
		float y = 0.0f;
		for (int i = 0; i < surface.size(); i++) {
			y += surface[i]->coordPara.y();
		}
		y /= surface.size();
		//y = (float)(int)y;
		//if (surface[0]->normalExpect == Vector3f(0, 1, 0)) {
		//	y = y + 1;
		//}
		//else {
		//	y = y - 1;
		//}
		for (int i = 0; i < surface.size(); i++) {
			surface[i]->coordPara.y() = y;
		}
	}
	else if (surface[0]->normalExpect == Vector3f(0, 0, 1) || surface[0]->normalExpect == Vector3f(0, 0, -1)) {
		float z = 0.0f;
		for (int i = 0; i < surface.size(); i++) {
			z += surface[i]->coordPara.z();
		}
		z /= surface.size();
		//z = (float)(int)z;
		//if (surface[0]->normalExpect == Vector3f(0, 0, 1)) {
		//	z = z + 1;
		//}
		//else {
		//	z = z - 1;
		//}
		for (int i = 0; i < surface.size(); i++) {
			surface[i]->coordPara.z() = z;
		}
	}

	//MatrixXf L = MatrixXf::Zero(stl->LoadedHE_Points.size(), stl->LoadedHE_Points.size());
	//MatrixXf R = MatrixXf::Zero(stl->LoadedHE_Points.size(), 3);
	//Eigen::MatrixXf NewCoord = MatrixXf::Zero(stl->LoadedHE_Points.size(), 3);
	//ComputeL(L,surface);
	//ComputeR(R,surface);
	//
	//*VectorXf phi_b = Eigen::VectorXf::Zero(stl->LoadedHE_Points.size());
	//VectorXf rho = Eigen::VectorXf::Zero(stl->LoadedHE_Points.size());*/
	//
	//Eigen::SparseMatrix<float> L_sparse(stl->LoadedHE_Points.size(), stl->LoadedHE_Points.size());
	//Eigen::SparseMatrix<float> R_sparse(stl->LoadedHE_Points.size(), 3);
	//Eigen::LeastSquaresConjugateGradient<Eigen::SparseMatrix<float>> Solver_sparse;
	//
	//buildSparseMatrix(L_sparse, L, stl->LoadedHE_Points.size(), stl->LoadedHE_Points.size());
	//buildSparseMatrix(R_sparse, R, stl->LoadedHE_Points.size(), 3);
	//
	//// 设置迭代精度
	//Solver_sparse.setTolerance(0.0001f);
	//Solver_sparse.compute(L_sparse);
	//
	//NewCoord = Solver_sparse.solve(R_sparse);
	//
	////将newCoord赋给坐标
	//for (int i = 0; i < stl->LoadedHE_Points.size(); i++) {
	//	if (stl->LoadedHE_Points[i]->surfaceType != 0) {
	//		stl->LoadedHE_Points[i]->coordPara.x() = NewCoord(i, 0);
	//		stl->LoadedHE_Points[i]->coordPara.y() = NewCoord(i, 1);
	//		stl->LoadedHE_Points[i]->coordPara.z() = NewCoord(i, 2);
	//	}
	//}
}
void PolyCubeParameterization::ComputeL(Eigen::MatrixXf& L, vector<HE_Point*>& surface) {
	//构建delta坐标系
	for (int i = 0; i < stl->LoadedHE_Points.size(); i++) {
		//(i,i) = 1
		L(i, i) = 1;

		vector<HE_Point*> temp = He_Mesh->GetVertexsFromVertex(stl->LoadedHE_Points[i]);
		vector<float> weights;
		float weightSum = 0;

		//计算(i,j)的均匀/cot/tan权重
		for (int j = 0; j < temp.size(); j++) {
			weights.push_back(GetWeight(stl->LoadedHE_Points[i], temp[j], Uniform));
			weightSum += weights[j];
		}

		//(i,j) = -weight
		for (int j = 0; j < temp.size(); j++) {
			float weight = weights[j] / weightSum;
			L(i, temp[j]->index) = -weight;
		}
	}
}
void PolyCubeParameterization::ComputeR(Eigen::MatrixXf& R, vector<HE_Point*>& surface) {
	if (surface[0]->normalExpect == Vector3f(1, 0, 0) || surface[0]->normalExpect == Vector3f(-1, 0, 0)) {
		float x = 0.0f;
		for (int i = 0; i < surface.size(); i++) {
			x += surface[i]->coordPara.x();
		}
		x /= surface.size();
		//x = (float)(int)x;
		//if (surface[0]->normalExpect == Vector3f(1, 0, 0)) {
		//	x = x + 1;
		//}
		//else {
		//	x = x - 1;
		//}
		for (int i = 0; i < surface.size(); i++) {
			R(surface[i]->index, 0) = x;
			R(surface[i]->index, 1) = surface[i]->coordPara.y();
			R(surface[i]->index, 2) = surface[i]->coordPara.z();
		}
	}
	else if (surface[0]->normalExpect == Vector3f(0, 1, 0) || surface[0]->normalExpect == Vector3f(0, -1, 0)) {
		float y = 0.0f;
		for (int i = 0; i < surface.size(); i++) {
			y += surface[i]->coordPara.y();
		}
		y /= surface.size();
		/*y = (float)(int)y;
		if (surface[0]->normalExpect == Vector3f(0, 1, 0)) {
			y = y + 1;
		}
		else {
			y = y - 1;
		}*/
		for (int i = 0; i < surface.size(); i++) {
			R(surface[i]->index, 0) = surface[i]->coordPara.x();
			R(surface[i]->index, 1) = y;
			R(surface[i]->index, 2) = surface[i]->coordPara.z();
		}
	}
	else if (surface[0]->normalExpect == Vector3f(0, 0, 1) || surface[0]->normalExpect == Vector3f(0, 0, -1)) {
		float z = 0.0f;
		for (int i = 0; i < surface.size(); i++) {
			z += surface[i]->coordPara.z();
		}
		z /= surface.size();
		/*z = (float)(int)z;
		if (surface[0]->normalExpect == Vector3f(0, 0, 1)) {
			z = z + 1;
		}
		else {
			z = z - 1;
		}*/
		for (int i = 0; i < surface.size(); i++) {
			R(surface[i]->index, 0) = surface[i]->coordPara.x();
			R(surface[i]->index, 1) = surface[i]->coordPara.y();
			R(surface[i]->index, 2) = z;
		}
	}

	/*for (int i = 0; i < surface.size(); i++) {
		HE_Point* point = stl->LoadedHE_Points[i];

		MatrixXf gradientI = MatrixXf::Zero(3, 3);
		ComputeRotateMatrix(gradientI, *point);

		vector<HE_Point*> points = He_Mesh->GetVertexsFromVertex(point);
		MatrixXf ans = MatrixXf::Zero(3, 1);

		for (int j = 0; j < points.size(); j++) {
			MatrixXf gradientJ = MatrixXf::Zero(3, 3);
			ComputeRotateMatrix(gradientJ, *points[j]);

			MatrixXf temp1 = MatrixXf::Zero(3, 3);
			temp1 = (gradientI + gradientJ) / 2;

			MatrixXf temp2 = MatrixXf::Zero(3, 1);
			temp2 = (point->coordPara - points[j]->coordPara);

			ans += temp1 * temp2;
		}

		R(i, 0) = ans(0, 0) / points.size();
		R(i, 1) = ans(1, 0) / points.size();
		R(i, 2) = ans(2, 0) / points.size();
	}*/
}

void PolyCubeParameterization::ComputeDistortion(distortionType type) {
	float avg = 0;
	for (int i = 0; i < stl->LoadedHE_Facets.size(); i++) {
		vector<HE_Point*> points;
		for (int j = 0; j < stl->LoadedHE_Facets[i]->PointIndices.size(); j++) {
			points.push_back(stl->LoadedHE_Points[stl->LoadedHE_Facets[i]->PointIndices[j]]);
		}

		//计算面积
		Vector3f e1 = points[1]->coord - points[0]->coord;
		Vector3f e2 = points[2]->coord - points[0]->coord;
		Vector3f avec = e1.cross(e2);
		stl->LoadedHE_Facets[i]->area = avec.norm() / 2.0f;

		//获取参数化前后的局部坐标
		vector<Vector2f> localCoords = GetLocalCoords(stl->LoadedHE_Facets[i], points, false);
		vector<Vector2f> localCoordsPara = GetLocalCoords(stl->LoadedHE_Facets[i], points, true);

		//Mt矩阵(Polygon Mesh Processing P73)
		Eigen::MatrixXf Mt(2, 3);
		Mt(0, 0) = localCoords[1].y() - localCoords[2].y();   Mt(0, 1) = localCoords[2].y() - localCoords[0].y();   Mt(0, 2) = localCoords[0].y() - localCoords[1].y();
		Mt(1, 0) = localCoords[2].x() - localCoords[1].x();   Mt(1, 1) = localCoords[0].x() - localCoords[2].x();   Mt(1, 2) = localCoords[1].x() - localCoords[0].x();
		Mt = (0.5f / stl->LoadedHE_Facets[i]->area) * Mt;

		//构建u向量(Polygon Mesh Processing P73)
		Eigen::MatrixXf UCoords(3, 1);
		UCoords(0, 0) = localCoordsPara[0].x();   UCoords(1, 0) = localCoordsPara[1].x();   UCoords(2, 0) = localCoordsPara[2].x();
		Eigen::MatrixXf UGradient = Mt * UCoords;

		////构建v向量(Polygon Mesh Processing P73)
		Eigen::MatrixXf VCoords(3, 1);
		VCoords(0, 0) = localCoordsPara[0].y();   VCoords(1, 0) = localCoordsPara[1].y();   VCoords(2, 0) = localCoordsPara[2].y();
		Eigen::MatrixXf VGradient = Mt * VCoords;

		//Polygon Mesh Processing P79
		Eigen::MatrixXf Jacobi(2, 2);
		Jacobi(0, 0) = UGradient(0, 0);   Jacobi(0, 1) = VGradient(0, 0);
		Jacobi(1, 0) = UGradient(1, 0);   Jacobi(1, 1) = VGradient(1, 0);

		//Polygon Mesh Processing P80
		Eigen::MatrixXf It = Jacobi.transpose() * Jacobi;
		
		//Polygon Mesh Processing P80
		float E = It(0, 0), F = It(1, 0), G = It(1, 1);
		float temp1 = 0.5 / (E + G), temp2 = sqrtf(pow(E - G, 2) + 4 * pow(F, 2));
		float a = sqrtf(temp1 + temp2);
		float b = sqrtf(temp1 - temp2);

		switch (type) {
		case Conformal:
			stl->LoadedHE_Facets[i]->distortion = b / a;
			break;
		case MaxIsometric:
			stl->LoadedHE_Facets[i]->distortion = max(b, 1 / a);
			break;
		case MIPS:
			stl->LoadedHE_Facets[i]->distortion = a / b + b / a;
			break;
		case Isometric:
			stl->LoadedHE_Facets[i]->distortion = sqrt(b * b + 1 / a / a);
			break;
		case SymmetricDiricheletEnergy:
			stl->LoadedHE_Facets[i]->distortion = a * a + 1 / (a * a) + b * b + 1 / (b * b);
			break;
		}

		avg += stl->LoadedHE_Facets[i]->distortion;
	}
	cout << avg / stl->LoadedHE_Facets.size() << endl;
}
vector<Vector2f> PolyCubeParameterization::GetLocalCoords(HE_Facet* facet, vector<HE_Point*>& points, bool isPara) {
	vector<Vector3f> temp;
	if (isPara) {
		for (int i = 0; i < points.size(); i++) {
			temp.push_back(points[i]->coordPara);
		}
	}
	else {
		for (int i = 0; i < points.size(); i++) {
			temp.push_back(points[i]->coord);
		}
	}

	Vector3f xAxis = (temp[1] - temp[0]).normalized();
	Vector3f normal = xAxis.cross(temp[2] - temp[0]).normalized();
	Vector3f yAxis = normal.cross(xAxis);

	vector<Vector2f> localCoords;
	for (int j = 0; j < temp.size(); j++) {
		//Vector2f localCoord = Vector2f(ProjV3f(temp[j], xAxis).norm(), ProjV3f(temp[j], yAxis).norm());
		Vector2f localCoord = Vector2f(temp[j].dot(xAxis), temp[j].dot(yAxis));
		localCoords.push_back(localCoord);
	}

	return localCoords;
}