//常量缓冲区是一种GPU资源,其内容可供着色器程序所引用,绘制n个物体,需要n个该类型的常量缓冲区
//由CPU每帧更新一次,因此创建到一个上传堆,且大小必为硬件最小分配空间(256B)的整数值
cbuffer cbPerObj : register(b0) {
	float4x4 gWorldViewProj;
	float4 gPulseColor;	//HW
	float gTime;		//HW
}

struct VertexIn {
	//参数语义":POSITION"和":COLOR"用于将顶点结构体中的元素映射到顶点着色器的相应输入参数
	float3 PosL : POSITION;
	float4 Color : COLOR;
};

struct VertexOut {
	//SV代表system value,它所修饰的顶点着色器输出元素存有齐次裁剪空间中的顶点位置信息
	//为输出位置信息的参数附上SV_POSITION,使GPU能在进行裁剪/深度测试/光栅化..时,实现其他属性无法介入的有关运算
	//如果没有使用几何着色器,那么顶点着色器必须用SV_POSITION语义来输出顶点在齐次裁剪空间中的位置
	float4 PosH : SV_POSITION;
	float4 Color : COLOR;
};

VertexOut VS(VertexIn vin) {
	VertexOut vout;

	//HW
	vin.PosL.xy += 0.5f * sin(vin.PosL.x) * sin(3.0f * gTime);
	vin.PosL.z *= 0.6f + 0.4f * sin(2.0f * gTime);

	//把顶点变换到齐次裁剪空间
	vout.PosH = mul(float4(vin.PosL, 1.0f), gWorldViewProj);

	//将顶点颜色传入像素着色器
	vout.Color = vin.Color;

	return vout;
}


float4 PS(VertexOut pin) : SV_Target{
	const float pi = 3.14159;

	float s = 0.5f * sin(2 * gTime - 0.25f * pi) + 0.5f;
	float4 c = lerp(pin.Color, gPulseColor, s);
	return c;
}