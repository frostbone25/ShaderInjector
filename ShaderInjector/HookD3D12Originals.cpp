#include "HookD3D12.h"

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
}
