#include "HookD3D12.h"

namespace HookD3D12
{
	PresentD3D12                  Original_PresentD3D12 = nullptr;
	Present1Fn                    Original_Present1D3D12 = nullptr;
	D3D12CreateDeviceFn           Original_D3D12CreateDevice = nullptr;
	ExecuteCommandListsFn         Original_ExecuteCommandListsD3D12 = nullptr;
	ResizeBuffersFn               Original_ResizeBuffersD3D12 = nullptr;
	PFN_CreatePipelineLibrary     Original_CreatePipelineLibrary = nullptr;
	PFN_LoadGraphicsPipeline      Original_LoadGraphicsPipeline = nullptr;
	PFN_LoadComputePipeline       Original_LoadComputePipeline = nullptr;
	PFN_LoadPipeline              Original_LoadPipeline = nullptr;
	PFN_StorePipeline             Original_StorePipeline = nullptr;
	PFN_GetSerializedSize         Original_GetSerializedSize = nullptr;
	PFN_Serialize                 Original_Serialize = nullptr;
	SetPipelineStateFn            Original_SetPipelineState = nullptr;
	ResetGraphicsCommandListFn    Original_ResetGraphicsCommandList = nullptr;
	SetGraphicsRootSignatureFn    Original_SetGraphicsRootSignature = nullptr;
	SetComputeRootSignatureFn     Original_SetComputeRootSignature = nullptr;
	CreateComputePipelineStateFn  Original_CreateComputePipelineState = nullptr;
	CreateRootSignatureFn         Original_CreateRootSignature = nullptr;
	CreateGraphicsPipelineStateFn Original_CreateGraphicsPipelineState = nullptr;
	CreatePipelineStateFn         Original_CreatePipelineState = nullptr;
}
