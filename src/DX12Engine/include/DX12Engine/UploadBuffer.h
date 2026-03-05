#pragma once

#include "DX12Engine/D3DUtil.h"

//封装上传缓冲区相关操作
template<typename T>
class UploadBuffer{
private:
    Microsoft::WRL::ComPtr<ID3D12Resource> uploadBuffer;
    BYTE* mappedData = nullptr;

    UINT elementByteSize = 0;
    bool isConstantBuffer = false;

public:
    //构造函数
    UploadBuffer(ID3D12Device* device, UINT elementCount, bool isConstantBuffer) : isConstantBuffer(isConstantBuffer){
        elementByteSize = sizeof(T);

        if(isConstantBuffer)
            elementByteSize = dx12Util::CalcConstantBufferByteSize(sizeof(T));
        
        //创建常量缓冲区
        ThrowIfFailed(
            device->CreateCommittedResource(
                &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
                D3D12_HEAP_FLAG_NONE,
                &CD3DX12_RESOURCE_DESC::Buffer(elementByteSize*elementCount),
			    D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&uploadBuffer)
            )
        );

        ThrowIfFailed(uploadBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mappedData)));

        //只要还会修改当前的资源,我们就无须取消映射
        //但是,在资源被GPU使用期间,我们千万不可向资源进行写操作
    }

    UploadBuffer(const UploadBuffer& rhs) = delete;
    UploadBuffer& operator=(const UploadBuffer& rhs) = delete;

    //析构函数
    ~UploadBuffer(){
        //更新完后,释放映射内存之前对其进行Unmap操作
        if(uploadBuffer != nullptr)
            uploadBuffer->Unmap(0, nullptr);

        mappedData = nullptr;
    }

    ID3D12Resource* Resource()const{
        return uploadBuffer.Get();
    }

    //更新缓冲区中的元素，CPU修改上传缓冲区中的数据时使用
    void CopyData(int elementIndex, const T& data){
        //将数据从系统内存(CPU端)复制到常量缓冲区
        memcpy(&mappedData[elementIndex*elementByteSize], &data, sizeof(T));
    }

};
