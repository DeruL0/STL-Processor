//物体相关常量缓冲区
cbuffer cbPerObject : register(b0){
	float4x4 world;
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
};

struct VertexIn{
    float3 PosL  : POSITION;
    float4 Color : COLOR;
};

struct VertexOut{
    float4 PosH  : SV_POSITION;
    float4 Color : COLOR;
};

VertexOut VS(VertexIn vin){
    VertexOut vout;

    float4 posW = mul(float4(vin.PosL, 1.0f), world);
    vout.PosH = mul(posW, viewProj);

    vout.Color = vin.Color;

    return vout;
}

float4 PS(VertexOut pin) : SV_Target{
    return pin.Color;
}
