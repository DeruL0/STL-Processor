//***************************************************************************************
// MathHelper.cpp by Frank Luna (C) 2011 All Rights Reserved.
//***************************************************************************************

#include "MathHelper.h"
#include <float.h>
#include <cmath>

using namespace DirectX;

const float MathHelper::Infinity = FLT_MAX;
const float MathHelper::Pi       = 3.1415926535f;

float MathHelper::AngleFromXY(float x, float y){
	float theta = 0.0f;
 
	// Quadrant I or IV
	if(x >= 0.0f) 
	{
		// If x = 0, then atanf(y/x) = +pi/2 if y > 0
		//                atanf(y/x) = -pi/2 if y < 0
		theta = atanf(y / x); // in [-pi/2, +pi/2]

		if(theta < 0.0f)
			theta += 2.0f*Pi; // in [0, 2*pi).
	}

	// Quadrant II or III
	else      
		theta = atanf(y/x) + Pi; // in [0, 2*pi).

	return theta;
}

XMVECTOR MathHelper::RandUnitVec3(){
	XMVECTOR One  = XMVectorSet(1.0f, 1.0f, 1.0f, 1.0f);
	XMVECTOR Zero = XMVectorZero();

	// Keep trying until we get a point on/in the hemisphere.
	while(true)
	{
		// Generate random point in the cube [-1,1]^3.
		XMVECTOR v = XMVectorSet(MathHelper::RandF(-1.0f, 1.0f), MathHelper::RandF(-1.0f, 1.0f), MathHelper::RandF(-1.0f, 1.0f), 0.0f);

		// Ignore points outside the unit sphere in order to get an even distribution 
		// over the unit sphere.  Otherwise points will clump more on the sphere near 
		// the corners of the cube.

		if( XMVector3Greater( XMVector3LengthSq(v), One) )
			continue;

		return XMVector3Normalize(v);
	}
}

XMVECTOR MathHelper::RandHemisphereUnitVec3(XMVECTOR n){
	XMVECTOR One  = XMVectorSet(1.0f, 1.0f, 1.0f, 1.0f);
	XMVECTOR Zero = XMVectorZero();

	// Keep trying until we get a point on/in the hemisphere.
	while(true)
	{
		// Generate random point in the cube [-1,1]^3.
		XMVECTOR v = XMVectorSet(MathHelper::RandF(-1.0f, 1.0f), MathHelper::RandF(-1.0f, 1.0f), MathHelper::RandF(-1.0f, 1.0f), 0.0f);

		// Ignore points outside the unit sphere in order to get an even distribution 
		// over the unit sphere.  Otherwise points will clump more on the sphere near 
		// the corners of the cube.
		
		if( XMVector3Greater( XMVector3LengthSq(v), One) )
			continue;

		// Ignore points in the bottom hemisphere.
		if( XMVector3Less( XMVector3Dot(n, v), Zero ) )
			continue;

		return XMVector3Normalize(v);
	}
}

XMFLOAT3 MathHelper::CalTriNormal(DirectX::XMFLOAT3 v0, DirectX::XMFLOAT3 v1, DirectX::XMFLOAT3 v2) {
	XMFLOAT3 u = SubVector(v1, v0);
	XMFLOAT3 v = SubVector(v2, v1);
	XMFLOAT3 normal = CrossVector(u, v);
	return Normalize(normal);
}

XMFLOAT3 MathHelper::SubVector(DirectX::XMFLOAT3 v0, DirectX::XMFLOAT3 v1) {
	return XMFLOAT3(v0.x - v1.x, v0.y - v1.y, v0.z - v1.z);
}

XMFLOAT3 MathHelper::CrossVector(DirectX::XMFLOAT3 v0, DirectX::XMFLOAT3 v1) {
	XMFLOAT3 crossProduct;
	crossProduct.x = v0.y * v1.z - v0.z * v1.y;
	crossProduct.y = v0.z * v1.x - v0.x * v1.z;
	crossProduct.z = v0.x * v1.y - v0.y * v1.x;
	return crossProduct;
}

float MathHelper::DotVector(DirectX::XMFLOAT3 v0, DirectX::XMFLOAT3 v1) {
	float dotProduct;
	dotProduct = v0.x * v1.x + v0.y * v1.y + v0.z * v1.z;
	return dotProduct;
}

XMFLOAT3 MathHelper::Normalize(DirectX::XMFLOAT3 v0) {
	float length = sqrt(v0.x * v0.x + v0.y * v0.y + v0.z * v0.z);
	if (length != 0.0f) {
		float invLength = 1.0f / length;
		return XMFLOAT3(v0.x * invLength, v0.y * invLength, v0.z * invLength);
	}
	else {
		return XMFLOAT3(0.0f, 0.0f, 0.0f);
	}
}

float MathHelper::GetTriArea(DirectX::XMFLOAT3 v0, DirectX::XMFLOAT3 v1, DirectX::XMFLOAT3 v2) {
	DirectX::XMFLOAT3 v0v1 = DirectX::XMFLOAT3(v1.x-v0.x, v1.y - v0.y, v1.z - v0.z);
	DirectX::XMFLOAT3 v0v2 = DirectX::XMFLOAT3(v2.x - v0.x, v2.y - v0.y, v2.z - v0.z);
	DirectX::XMFLOAT3 cross = CrossVector(v0v1, v0v2);
	float area = 0.5 * std::sqrt(cross.x * cross.x + cross.y * cross.y + cross.z * cross.z);
	return area;

}\

float MathHelper::AngleBetweenVectors(DirectX::XMFLOAT3 v0, DirectX::XMFLOAT3 v1) {
	float cos = DotVector(v0, v1) / (sqrt(v0.x * v0.x + v0.y * v0.y + v0.z * v0.z) * sqrt(v1.x * v1.x + v1.y * v1.y + v1.z * v1.z));
	return acosf(cos);
}
