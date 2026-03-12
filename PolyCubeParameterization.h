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

class PolyCubeParameterization {
public:
	STL* stl;
	HE_Mesh* He_Mesh;

	PolyCubeParameterization(STL& _stl) : stl(&_stl) {};
	~PolyCubeParameterization();

	vector<HE_Point*> Corners;
	vector<HE_Point*> BoundaryPoints;
	vector<HE_Edge*> BoundaryEdges;
	vector<vector<HE_Point*>*> Surfaces;
	vector<Vector3f> Axes;

public:
	void Parameterize();

private:
	void GetFacetsArea();
	float GetFacetArea(int i);

	void Smoothing(int n, float step);
	float GetWeight(HE_Point* point, HE_Point* neighbor, weightType type);

	void DyeVertexes();
	Vector3f GetVertexColor(Vector3f normal);

	void LoadFacetsSeeds(string file_path, string file_name);
	Vector3f GetFacetColorFromType(int type);
	
	void RotateVertexes(int n);
	void ComputeA(Eigen::MatrixXf& A);
	void ComputeB(Eigen::MatrixXf& B);
	void ComputeRotateMatrix(Eigen::MatrixXf& matrix, HE_Point& point);
	void buildSparseMatrix(Eigen::SparseMatrix<float>& A1_sparse, Eigen::MatrixXf A, int A_rows, int A_cols);
	
	void SetSurfacesType();
	void SetSufaceType(vector<HE_Point*>& surface, int facetType);

	void SetSurfacesCoord();
	void SetSurfaceCoord(vector<HE_Point*>& surface);
	void ComputeL(Eigen::MatrixXf& L, vector<HE_Point*>& surface);
	void ComputeR(Eigen::MatrixXf& R, vector<HE_Point*>& surface);

	void ComputeDistortion(distortionType type);
	vector<Vector2f> GetLocalCoords(HE_Facet* facet, vector<HE_Point*>& points, bool isPara);
};

