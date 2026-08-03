//HookD3D12Originals.cpp
#include "HookD3D12.h"
#include "HookD3D12RenderPass.h"
#include "HookD3D12Resources.h"

namespace HookD3D12
{
	FunctionPresentD3D12                     Original_PresentD3D12 = nullptr;
	FunctionPresent1D3D12                    Original_Present1D3D12 = nullptr;
	FunctionCreateDeviceD3D12                Original_CreateDeviceD3D12 = nullptr;
	FunctionExecuteCommandListsD3D12         Original_ExecuteCommandListsD3D12 = nullptr;
	FunctionResizeBuffersD3D12               Original_ResizeBuffersD3D12 = nullptr;
	FunctionCreatePipelineLibraryD3D12       Original_CreatePipelineLibrary = nullptr;
	FunctionLoadGraphicsPipelineD3D12        Original_LoadGraphicsPipeline = nullptr;
	FunctionLoadComputePipelineD3D12         Original_LoadComputePipeline = nullptr;
	FunctionLoadPipelineD3D12                Original_LoadPipeline = nullptr;
	FunctionStorePipelineD3D12               Original_StorePipeline = nullptr;
	FunctionGetSerializedSizeD3D12           Original_GetSerializedSize = nullptr;
	FunctionSerializeD3D12                   Original_Serialize = nullptr;
	FunctionSetPipelineStateD3D12            Original_SetPipelineState = nullptr;
	FunctionResetGraphicsCommandListD3D12    Original_ResetGraphicsCommandList = nullptr;
	FunctionSetGraphicsRootSignatureD3D12    Original_SetGraphicsRootSignature = nullptr;
	FunctionSetComputeRootSignatureD3D12     Original_SetComputeRootSignature = nullptr;
	FunctionCreateComputePipelineStateD3D12  Original_CreateComputePipelineState = nullptr;
	FunctionCreateRootSignatureD3D12         Original_CreateRootSignature = nullptr;
	FunctionCreateGraphicsPipelineStateD3D12 Original_CreateGraphicsPipelineState = nullptr;
	FunctionCreatePipelineStateD3D12         Original_CreatePipelineState = nullptr;
	FunctionDrawInstancedD3D12                Original_DrawInstanced = nullptr;
	FunctionDrawIndexedInstancedD3D12         Original_DrawIndexedInstanced = nullptr;
	FunctionDispatchD3D12                     Original_Dispatch = nullptr;
	FunctionIASetPrimitiveTopologyD3D12       Original_IASetPrimitiveTopology = nullptr;
	FunctionRSSetViewportsD3D12               Original_RSSetViewports = nullptr;
	FunctionRSSetScissorRectsD3D12            Original_RSSetScissorRects = nullptr;
	FunctionSetDescriptorHeapsD3D12           Original_SetDescriptorHeaps = nullptr;
	FunctionSetRootDescriptorTableD3D12       Original_SetComputeRootDescriptorTable = nullptr;
	FunctionSetRootDescriptorTableD3D12       Original_SetGraphicsRootDescriptorTable = nullptr;
	FunctionSetRoot32BitConstantD3D12         Original_SetComputeRoot32BitConstant = nullptr;
	FunctionSetRoot32BitConstantD3D12         Original_SetGraphicsRoot32BitConstant = nullptr;
	FunctionSetRoot32BitConstantsD3D12        Original_SetComputeRoot32BitConstants = nullptr;
	FunctionSetRoot32BitConstantsD3D12        Original_SetGraphicsRoot32BitConstants = nullptr;
	FunctionSetRootDescriptorD3D12            Original_SetComputeRootConstantBufferView = nullptr;
	FunctionSetRootDescriptorD3D12            Original_SetGraphicsRootConstantBufferView = nullptr;
	FunctionSetRootDescriptorD3D12            Original_SetComputeRootShaderResourceView = nullptr;
	FunctionSetRootDescriptorD3D12            Original_SetGraphicsRootShaderResourceView = nullptr;
	FunctionSetRootDescriptorD3D12            Original_SetComputeRootUnorderedAccessView = nullptr;
	FunctionSetRootDescriptorD3D12            Original_SetGraphicsRootUnorderedAccessView = nullptr;
	FunctionIASetIndexBufferD3D12              Original_IASetIndexBuffer = nullptr;
	FunctionIASetVertexBuffersD3D12            Original_IASetVertexBuffers = nullptr;
	FunctionOMSetRenderTargetsD3D12            Original_OMSetRenderTargets = nullptr;
	FunctionExecuteIndirectD3D12               Original_ExecuteIndirect = nullptr;
	FunctionCreateConstantBufferViewD3D12      Original_CreateConstantBufferView = nullptr;
	FunctionCreateShaderResourceViewD3D12      Original_CreateShaderResourceView = nullptr;
	FunctionCreateUnorderedAccessViewD3D12     Original_CreateUnorderedAccessView = nullptr;
	FunctionCreateRenderTargetViewD3D12        Original_CreateRenderTargetView = nullptr;
	FunctionCreateDepthStencilViewD3D12        Original_CreateDepthStencilView = nullptr;
	FunctionCreateSamplerD3D12                 Original_CreateSampler = nullptr;
	FunctionCopyDescriptorsD3D12               Original_CopyDescriptors = nullptr;
	FunctionCopyDescriptorsSimpleD3D12         Original_CopyDescriptorsSimple = nullptr;
	FunctionCreateCommittedResourceD3D12       Original_CreateCommittedResource = nullptr;
	FunctionCreatePlacedResourceD3D12          Original_CreatePlacedResource = nullptr;
	FunctionCreateReservedResourceD3D12        Original_CreateReservedResource = nullptr;
}
