//默认光源数
#ifndef NUM_DIR_LIGHTS
#define NUM_DIR_LIGHTS 3
#endif

#ifndef NUM_POINT_LIGHTS
#define NUM_POINT_LIGHTS 0
#endif

#ifndef NUM_SPOT_LIGHTS
#define NUM_SPOT_LIGHTS 0
#endif

#include "LightUtil.hlsl"

struct MaterialData{
    float4 DiffuseAlbedo;
    float3 FresnelR0;
    float Roughness;
    float4x4 MatTransform;
    uint DiffuseMapIndex;
    uint MatPad0;
    uint MatPad1;
    uint MatPad2;
};

// An array of textures, which is only supported in shader model 5.1+.  Unlike Texture2DArray, the textures
// in this array can be different sizes and formats, making it more flexible than texture arrays.
Texture2D diffuseMap[4] : register(t0);

// Put in space1, so the texture array does not overlap with these resources.  
// The texture array will occupy registers t0, t1, ..., t3 in space0. 
StructuredBuffer<MaterialData> materialData : register(t0, space1);

SamplerState samPointWrap : register(s0);
SamplerState samPointClamp : register(s1);
SamplerState samLinearWrap : register(s2);
SamplerState samLinearClamp : register(s3);
SamplerState samAnisotropicWrap : register(s4);
SamplerState samAnisotropicClamp : register(s5);


//物体相关常量缓冲区
cbuffer cbPerObject : register(b0){
    float4x4 world;
    float4x4 texTransform;
    uint materialIndex;
    uint objPad0;
    uint objPad1;
    uint objPad2;
};

//观察位置，观察矩阵，投影矩阵，屏幕分辨率的渲染常量缓冲区
//每次更新只需要更新cbPass，cbPerObject只有当物体位置变动时更新
cbuffer cbPass : register(b1){
    float4x4 view;
    float4x4 invView;
    float4x4 proj;
    float4x4 invProj;
    float4x4 viewProj;
    float4x4 invViewProj;
    float3 eyePosW;
    float cbPerObjectPad1;
    float2 renderTargetSize;
    float2 invRenderTargetSize;
    float nearZ;
    float farZ;
    float totalTime;
    float deltaTime;
    
    //[0, NUM_DIR_LIGHTS)为方向光源
    //[NUM_DIR_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHTS)为点光源
    //[NUM_DIR_LIGHTS+NUM_POINT_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHT+NUM_SPOT_LIGHTS)为聚光灯源
    float4 ambientLight;
    Light lights[MaxLights];
};

struct VertexIn{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC : TEXCOORD;
};

struct VertexOut{
    float4 PosH : SV_POSITION;
    float3 PosW : POSITION;
    float3 NormalW : NORMAL;
    float2 TexC : TEXCOORD;
};

VertexOut VS(VertexIn vin){
    VertexOut vout = (VertexOut) 0.0f;

    // Fetch the material data.
    MaterialData matData = materialData[materialIndex];
    
    float4 posW = mul(float4(vin.PosL, 1.0f), world);
    vout.PosW = posW.xyz;

    //假设为等比缩放，否则要用世界矩阵的逆转置矩阵
    vout.NormalW = mul(vin.NormalL, (float3x3) world);
    
    //将顶点变换到齐次裁剪空间
    vout.PosH = mul(posW, viewProj);
    
    // Output vertex attributes for interpolation across triangle.
    float4 texC = mul(float4(vin.TexC, 0.0f, 1.0f), texTransform);
    vout.TexC = mul(texC, matData.MatTransform).xy;
	
    return vout;
}

float4 PS(VertexOut pin) : SV_Target{
	// Fetch the material data.
    MaterialData matData = materialData[materialIndex];
    float4 diffuseAlbedo = matData.DiffuseAlbedo;
    float3 fresnelR0 = matData.FresnelR0;
    float roughness = matData.Roughness;
    uint diffuseTexIndex = matData.DiffuseMapIndex;

	// Dynamically look up the texture in the array.
    diffuseAlbedo *= diffuseMap[diffuseTexIndex].Sample(samLinearWrap, pin.TexC);
    
    //对法线进行规范化，因为对其插值可能导致其非规范化
    pin.NormalW = normalize(pin.NormalW);
    
    //计算光经过表面一点反射到观察点方向的向量
    float3 toEyeW = normalize(eyePosW - pin.PosW);
    
    //间接光照
    float4 ambient = ambientLight * diffuseAlbedo;
    
    //直接光照
    const float shininess = 1.0f - roughness;
    Material mat = { diffuseAlbedo, fresnelR0, shininess };
    float3 shadowFactor = 1.0f;
    float4 directLight = ComputeLighting(lights, mat, pin.PosW, pin.NormalW, toEyeW, shadowFactor);
    
    float4 litColor = ambient + directLight;
    
    litColor.a = diffuseAlbedo.a;
    
    return litColor;
}
