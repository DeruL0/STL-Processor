#pragma once
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <math.h>
#include <sstream>
#include <unordered_set>
#include "BasicDefinitions.h"
#include "HE_Mesh.h"
#include "STL.h"
#include "Geometry.h"
#include "Eigen/Eigen"
using namespace std;
using namespace Eigen;

class STL;
class HE_Mesh;

class CubeParameterization {
public:
	STL* stl;
	HE_Mesh* He_Mesh;

	CubeParameterization(STL& _stl) : stl(&_stl) {};
	~CubeParameterization();

	vector<HE_Point*> Corners;
	vector<HE_Point*> BoundaryPoints;
	vector<HE_Edge*> BoundaryEdges;
	vector<vector<HE_Point*>*> Sufaces;

public:
	void Parameterize();

private:
	void LoadBoundary(string file_path, string file_name);
	void SetSurfacesType();
	void SetSufaceType(vector<HE_Point*>& surface, int facetType);

	void SetCornersCoord();

	void SetEdgesCoord();
	void SetEdgeCoord(HE_Point* start, HE_Point* end, vector<HE_Point*>& vertexs);
	vector<int> GetCornersFromEdge(int edgeType);

	void SetSurfacesCoord();
	void ComputeA(Eigen::MatrixXf& A);
	float GetWeight(HE_Point* point, HE_Point* neighbor, weightType type);
	void ComputeB(Eigen::MatrixXf& Boundary);
	void buildSparseMatrix(Eigen::SparseMatrix<float>& A1_sparse, Eigen::MatrixXf A, int A_rows, int A_cols);

	void ComputeDistortion(distortionType type);
	vector<Vector2f> GetLocalCoords(HE_Facet* facet, vector<HE_Point*>& points, bool isPara);
};
