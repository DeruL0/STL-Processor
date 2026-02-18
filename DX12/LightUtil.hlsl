#define MaxLights 16

struct Light{
    float3 Strength;
    float FalloffStart; // point/spot light only
    float3 Direction;   // directional/spot light only
    float FalloffEnd;   // point/spot light only
    float3 Position;    // point light only
    float SpotPower;    // spot light only
};

struct Material{
    float4 DiffuseAlbedo;
    float3 FresnelR0;
    float Shininess;
};

//线性衰减
float CalcAttenuation(float d, float falloffStart, float falloffEnd) {
    return saturate((falloffEnd - d) / (falloffEnd - falloffStart));
}

//石里克近似 -- 替代菲涅耳效应
//R0 = ( (n-1)/(n+1) )^2, n为折射率
float3 SchlickFresnel(float3 R0, float3 normal, float3 lightVec){
    float cosIncidentAngle = saturate(dot(normal, lightVec));
    
    float f0 = 1.0f - cosIncidentAngle;
    float3 reflectPercent = R0 + (1.0F - R0) * (f0 * f0 * f0 * f0);

    return reflectPercent;
}

//计算反射到观察者眼中的光亮，漫反射+镜面反射，不考虑环境光
float3 BlinnPhong(float3 lightStrength, float3 lightVec, float3 normal, float3 toEye, Material mat){
    const float m = mat.Shininess * 256.0f; //m由光泽度推导而来
    float3 halfVec = normalize(toEye + lightVec); 
    
    float roughnessFactor = (m + 8.0f) * pow(max(dot(halfVec, normal), 0.0f), m) / 8.0f;
    float3 fresnelFactor = SchlickFresnel(mat.FresnelR0, halfVec, lightVec);

    float specAlbedo = fresnelFactor * roughnessFactor;
    
    specAlbedo = specAlbedo / (specAlbedo + 1.0f);
    
    return (mat.DiffuseAlbedo.rgb + specAlbedo) * lightStrength;
}

//给定观察位置E，材质，以n为法线的表面可见一点p，输出某方向光源发出，以方向v=norm(E-p)反射入观察者的光亮

//方向光源
float3 ComputeDirectionalLight(Light L, Material mat, float3 normal, float3 toEye){
    //光向量与光传播方向相反
    float3 lightVec = -L.Direction;
    
    //根据兰伯特余弦定理按比例降低光强
    float ndotl = max(dot(lightVec, normal), 0.0f);
    float3 lightStrength = L.Strength * ndotl;
    
    return BlinnPhong(lightStrength, lightVec, normal, toEye, mat);
}

//点光源
float3 ComputePointLight(Light L, Material mat, float3 pos, float3 normal, float3 toEye){
     //光向量与光传播方向相反
    float3 lightVec = -L.Direction;
    
    //光到表面的距离
    float d = length(lightVec);
    
    //距离检测
    if (d > L.FalloffEnd){
        return 0.0f;
    }
    
    //规范化光向量
    lightVec /= d;
    
    //根据兰伯特余弦定理按比例降低光强
    float ndotl = max(dot(lightVec, normal), 0.0f);
    float3 lightStrength = L.Strength * ndotl;
    
    //根据距离计算光衰减
    float att = CalcAttenuation(d, L.FalloffStart, L.FalloffEnd);
    lightStrength *= att;
    
    return BlinnPhong(lightStrength, lightVec, normal, toEye, mat);
}

//聚光灯光源
float3 ComputeSpotLight(Light L, Material mat, float3 pos, float3 normal, float3 toEye){
    //光向量与光传播方向相反
    float3 lightVec = -L.Direction;
    
    //光到表面的距离
    float d = length(lightVec);
    
    //距离检测
    if (d > L.FalloffEnd){
        return 0.0f;
    }
    
    //规范化光向量
    lightVec /= d;
    
    //根据兰伯特余弦定理按比例降低光强
    float ndotl = max(dot(lightVec, normal), 0.0f);
    float3 lightStrength = L.Strength * ndotl;
    
    //根据距离计算光衰减
    float att = CalcAttenuation(d, L.FalloffStart, L.FalloffEnd);
    lightStrength *= att;
    
    //根据聚光灯模型对光亮进行缩放
    float spotFactor = pow(max(dot(-lightVec, L.Direction), 0.f), L.SpotPower);
    lightStrength *= spotFactor;
    
    return BlinnPhong(lightStrength, lightVec, normal, toEye, mat);
}

//光源累加
float4 ComputeLighting(Light gLights[MaxLights], Material mat, float3 pos, float3 normal, float3 toEye, float3 shadowFactor){
    float3 result = 0.0f;
    
    int i = 0;
    
    //分别计算不同类型的光源，#define来控制光源数量
#if (NUM_DIR_LIGHTS > 0)
    for(int i = 0; i < NUM_DIR_LIGHTS; ++i){
        result += shadowFactor[i] * ComputeDirectionalLight(gLights[i], mat, normal, toEye);
    }
#endif
#if (NUM_POINT_LIGHTS > 0)
    for(int i = NUM_DIR_LIGHTS; i < NUM_DIR_LIGHTS + NUM_POINT_LIGHTS; ++i){
        result += ComputePointLight(gLights[i], mat, normal, toEye);
    }
#endif
#if (NUM_SPOT_LIGHTS > 0)
    for(int i = NUM_DIR_LIGHTS + NUM_POINT_LIGHTS; i < NUM_DIR_LIGHTS + NUM_POINT_LIGHTS + NUM_SPOT_LIGHTS; ++i){
        result += ComputeDirectionalLight(gLights[i], mat, normal, toEye);
    }
#endif
    
    return float4(result, 0.0f);
}