#include "Dx12Pso.h"
#include "Dx12Device.h"
#include <Pegasus/Utils/Memset.h>
#include <atlbase.h>

namespace Pegasus
{
namespace Render
{

Dx12Pso::Dx12Pso(Dx12Device* device)
: Core::RefCounted(device->GetAllocator()), mDevice(device), mPso(nullptr), mType(PsoGraphics)
{
}

Dx12Pso::~Dx12Pso()
{
    if (mPso != nullptr)
    {
        mPso->Release();
    }
}

bool Dx12Pso::Compile(const Dx12PsoDesc& desc, Dx12GpuProgramRef program)
{
    mDesc = desc;
    mProgram = program;

    if (mProgram == nullptr || !mProgram->IsValid())
        return false;

    if (mPso != nullptr)
    {
        mPso->Release();
        mPso = nullptr;
    }

    mType = mProgram->GetShaderByteCode(Dx12_Compute) != nullptr ? PsoCompute : PsoGraphics;

    if (mType == PsoGraphics)
    {
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
       // Utils::Memset8(&psoDesc, 0x0, sizeof(psoDesc));

        psoDesc.pRootSignature = mProgram->GetRootSignature();
        for (unsigned int i = 0; i < mProgram->GetShaderByteCodeCounts(); ++i)
        {
            Dx12PipelineType type;
            CComPtr<ID3DBlob> blob = mProgram->GetShaderByteCodeByIndex(i, type);
            D3D12_SHADER_BYTECODE* targetByteCode = nullptr;
            switch (type)
            {
            case Dx12_Vertex:
                targetByteCode = &psoDesc.VS;
                break;
            case Dx12_Pixel:
                targetByteCode = &psoDesc.PS;
                break;
            case Dx12_Domain:
                targetByteCode = &psoDesc.DS;
                break;
            case Dx12_Hull:
                targetByteCode = &psoDesc.HS;
                break;
            case Dx12_Geometry:
                targetByteCode = &psoDesc.GS;
                break;
            default:
                PG_FAILSTR("Invalid shader pipeline type");
            }
            
            targetByteCode->pShaderBytecode = blob->GetBufferPointer();
            targetByteCode->BytecodeLength = blob->GetBufferSize();
        }
        
		D3D12_INPUT_ELEMENT_DESC inputLayoutElements[] = { { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0u, 0u, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0u } };
		psoDesc.InputLayout = { inputLayoutElements, _countof(inputLayoutElements) };
		psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
		psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
        psoDesc.NumRenderTargets = 1u;
		psoDesc.SampleMask = 0xffffffff;
		psoDesc.SampleDesc.Count = 1u;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

// This is only available on Warp Devices. No idea how to use it :)? 
//#if PEGASUS_DEBUG
//        psoDesc.Flags |= D3D12_PIPELINE_STATE_FLAG_TOOL_DEBUG;
//#endif
        
        HRESULT result = mDevice->GetD3D()->CreateGraphicsPipelineState(&psoDesc, _uuidof(ID3D12PipelineState), reinterpret_cast<void**>(&mPso));
        if (result != S_OK)
            return false;
        else
            return true; 
    }
    else
    {
        return false;
    }
}

}
}