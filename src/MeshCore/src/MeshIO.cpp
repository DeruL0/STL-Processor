#include "MeshCore/HalfEdgeMesh.h"
#include "MeshMath.h"

#include <fstream>
#include <stdexcept>

//File Operations==========================================================================================================================
void HE_MeshData::ReadSTL(std::string file_path, std::string file_name) {
	Clear();

	file_path.append(file_name);
	file_path.append(".stl");
	
	std::ifstream fin(file_path);

	if (!fin) {
		throw std::runtime_error("Model not found: " + file_path);
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
			tempVertex.Pos = Vec3f(x, y, z);

			if (occ_vertexes.count(tempVertex) == 0) {
				occ_vertexes.insert(std::make_pair(tempVertex, vcount));

				HE_Vertex* temp = new HE_Vertex();
				temp->Index = vcount++;
				temp->Pos = Vec3f(x, y, z);
				temp->Normal = Vec3f(0.0f, 0.0f, 0.0f);

				HE_Vertexes.push_back(temp);
			}

			vertex_indices.push_back(occ_vertexes.at(tempVertex));
		}

		//get correct normal
		Vec3f correctNormal = CalTriNormal(
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

	for (std::uint32_t i = 0; i < HE_Triangles.size(); i++) {
		file << 3 << ' ' << HE_Triangles[i]->VertexIndex0 << ' ' << HE_Triangles[i]->VertexIndex1 << ' ' << HE_Triangles[i]->VertexIndex2 << std::endl;
	}

	//CELLDATA
	file << "CELL_DATA " << HE_Triangles.size() << std::endl;
	file << "SCALARS cell_scalars float 1  " << std::endl;
	file << "LOOKUP_TABLE default  " << std::endl;

	for (std::uint32_t i = 0; i < HE_Triangles.size(); i++) {
		file << i << std::endl;
	}

	//NORMALS
	file << "NORMALS cell_normals float" << std::endl;

	for (std::uint32_t i = 0; i < HE_Triangles.size(); i++) {
		file << HE_Triangles[i]->Normal.x << ' ' << HE_Triangles[i]->Normal.y << ' ' << HE_Triangles[i]->Normal.z << std::endl;
	}

	//Cellids
	file << "FIELD FieldData 2 " << std::endl;
	file << "cellIds 1 " << HE_Triangles.size() << " int " << std::endl;

	for (std::uint32_t i = 0; i < HE_Triangles.size(); i++) {
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

	for (std::uint32_t i = 0; i < HE_Triangles.size(); i++) {
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
