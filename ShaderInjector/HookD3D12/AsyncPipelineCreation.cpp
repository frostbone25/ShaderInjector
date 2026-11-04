#include "HookD3D12/AsyncPipelineCreation.h"

#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <utility>

#include "HookD3D12/HookD3D12.h"
#include "ModifiedShader/DatabaseModifiedShaders.h"

namespace HookD3D12::AsyncPipelineCreation
{
	namespace
	{
		enum class State
		{
			Pending,
			Ready,
			Failed
		};

		struct Ticket
		{
			std::mutex mutex;
			NativeCreateCallback createCallback;
			Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState;
			State state = State::Pending;
			HRESULT result = E_PENDING;
			ULONGLONG lastRequestedTick = 0;
			std::atomic<bool> started = false;
			std::atomic<bool> queued = false;
			std::atomic<bool> urgentRequestQueued = false;
			bool queueHookApplyOnCompletion = false;
		};

		constexpr size_t maximumPendingTicketCount = 64;
		constexpr ULONGLONG staleTicketAgeMilliseconds = 30000;
		std::mutex gTicketMutex;
		std::unordered_map<std::string, std::shared_ptr<Ticket>> gTickets;
		bool gCleanupQueued = false;

		template <typename Value>
		Value* DataOrNull(std::vector<Value>& values)
		{
			if (values.empty())
				return nullptr;

			return values.data();
		}

		template <typename Value>
		const Value* DataOrNull(const std::vector<Value>& values)
		{
			if (values.empty())
				return nullptr;

			return values.data();
		}

		void CopyBytecode(const D3D12_SHADER_BYTECODE& source, std::vector<uint8_t>& destination)
		{
			destination.clear();

			if (!source.pShaderBytecode || source.BytecodeLength == 0)
				return;

			const auto* firstByte = static_cast<const uint8_t*>(source.pShaderBytecode);
			destination.assign(firstByte, firstByte + source.BytecodeLength);
		}

		D3D12_SHADER_BYTECODE MakeBytecode(const std::vector<uint8_t>& bytecode)
		{
			return {DataOrNull(bytecode), bytecode.size()};
		}

		void CopyInputLayout(const D3D12_INPUT_LAYOUT_DESC& source, OwnedGraphicsPipelineDesc& destination)
		{
			destination.inputElements.clear();
			destination.inputElementSemanticNames.clear();

			if (source.NumElements > 0 && source.pInputElementDescs)
			{
				destination.inputElements.assign(source.pInputElementDescs, source.pInputElementDescs + source.NumElements);
				destination.inputElementSemanticNames.reserve(source.NumElements);

				for (const D3D12_INPUT_ELEMENT_DESC& element : destination.inputElements)
				{
					if (element.SemanticName)
						destination.inputElementSemanticNames.push_back(element.SemanticName);
					else
						destination.inputElementSemanticNames.emplace_back();
				}
			}

			for (size_t index = 0; index < destination.inputElements.size(); ++index)
			{
				if (destination.inputElementSemanticNames[index].empty())
					destination.inputElements[index].SemanticName = nullptr;
				else
					destination.inputElements[index].SemanticName = destination.inputElementSemanticNames[index].c_str();
			}
			destination.description.InputLayout = {DataOrNull(destination.inputElements), static_cast<UINT>(destination.inputElements.size())};
		}

		void CopyStreamOutput(const D3D12_STREAM_OUTPUT_DESC& source, OwnedGraphicsPipelineDesc& destination)
		{
			destination.streamOutputDeclarations.clear();
			destination.streamOutputSemanticNames.clear();
			destination.streamOutputStrides.clear();

			if (source.NumEntries > 0 && source.pSODeclaration)
			{
				destination.streamOutputDeclarations.assign(source.pSODeclaration, source.pSODeclaration + source.NumEntries);
				destination.streamOutputSemanticNames.reserve(source.NumEntries);

				for (const D3D12_SO_DECLARATION_ENTRY& declaration : destination.streamOutputDeclarations)
				{
					if (declaration.SemanticName)
						destination.streamOutputSemanticNames.push_back(declaration.SemanticName);
					else
						destination.streamOutputSemanticNames.emplace_back();
				}
			}

			if (source.NumStrides > 0 && source.pBufferStrides)
				destination.streamOutputStrides.assign(source.pBufferStrides, source.pBufferStrides + source.NumStrides);

			for (size_t index = 0; index < destination.streamOutputDeclarations.size(); ++index)
			{
				if (destination.streamOutputSemanticNames[index].empty())
					destination.streamOutputDeclarations[index].SemanticName = nullptr;
				else
					destination.streamOutputDeclarations[index].SemanticName = destination.streamOutputSemanticNames[index].c_str();
			}

			destination.description.StreamOutput.pSODeclaration = DataOrNull(destination.streamOutputDeclarations);
			destination.description.StreamOutput.NumEntries = static_cast<UINT>(destination.streamOutputDeclarations.size());
			destination.description.StreamOutput.pBufferStrides = DataOrNull(destination.streamOutputStrides);
			destination.description.StreamOutput.NumStrides = static_cast<UINT>(destination.streamOutputStrides.size());
		}

		void CleanupStaleTicketsOnWorker()
		{
			std::vector<Microsoft::WRL::ComPtr<ID3D12PipelineState>> stalePipelineStates;
			const ULONGLONG currentTick = GetTickCount64();

			{
				std::lock_guard<std::mutex> ticketLock(gTicketMutex);

				for (auto ticketIterator = gTickets.begin(); ticketIterator != gTickets.end();)
				{
					//keep the ticket alive until its state lock is released, even when this iteration erases its map entry.
					const std::shared_ptr<Ticket> ticket = ticketIterator->second;
					std::lock_guard<std::mutex> stateLock(ticket->mutex);

					const bool canceledNativeRequest = ticket->state == State::Failed && ticket->result == E_ABORT;
					const bool staleTicket = ticket->state != State::Pending && currentTick - ticket->lastRequestedTick > staleTicketAgeMilliseconds;
					if (canceledNativeRequest || staleTicket)
					{
						stalePipelineStates.push_back(std::move(ticket->pipelineState));
						ticketIterator = gTickets.erase(ticketIterator);
					}
					else
						++ticketIterator;
				}

				gCleanupQueued = false;
			}

			//the discarded COM references leave scope on this compiler worker.
		}

		void RunTicket(const std::shared_ptr<Ticket>& ticket)
		{
			ticket->queued = false;

			if (ticket->started.exchange(true))
				return;

			NativeCreateCallback createCallback;
			{
				std::lock_guard<std::mutex> stateLock(ticket->mutex);
				createCallback = std::move(ticket->createCallback);
			}

			Microsoft::WRL::ComPtr<ID3D12PipelineState> createdPipeline;
			ID3D12PipelineState* rawPipeline = nullptr;
			HRESULT result = E_FAIL;

			try
			{
				if (createCallback)
					result = createCallback(&rawPipeline);
			}
			catch (...)
			{
				result = E_FAIL;
			}

			if (rawPipeline)
				createdPipeline.Attach(rawPipeline);

			{
				std::lock_guard<std::mutex> stateLock(ticket->mutex);
				ticket->result = result;

				if (SUCCEEDED(result) && createdPipeline)
				{
					ticket->pipelineState = std::move(createdPipeline);
					ticket->state = State::Ready;
				}
				else
					ticket->state = State::Failed;

			}

			CleanupStaleTicketsOnWorker();

			if (ticket->queueHookApplyOnCompletion)
				HookD3D12::QueueShaderTargetApplyWork();
		}

		bool EnqueueTicket(const std::shared_ptr<Ticket>& ticket, bool urgent, bool allowDuplicate = false)
		{
			if (!allowDuplicate && ticket->queued.exchange(true))
				return true;

			const bool queued = DatabaseModifiedShaders::QueueBackgroundCompile(
				[ticket]()
				{
					RunTicket(ticket);
				},
				urgent,
				{});

			if (!queued && !allowDuplicate)
				ticket->queued = false;

			return queued;
		}

		void ScheduleCleanup()
		{
			{
				std::lock_guard<std::mutex> ticketLock(gTicketMutex);

				if (gCleanupQueued)
					return;

				gCleanupQueued = true;
			}

			if (!DatabaseModifiedShaders::QueueBackgroundCompile(CleanupStaleTicketsOnWorker, true))
			{
				std::lock_guard<std::mutex> ticketLock(gTicketMutex);
				gCleanupQueued = false;
			}
		}
	}

	bool OwnedGraphicsPipelineDesc::Initialize(
		ID3D12PipelineState* original,
		const D3D12_GRAPHICS_PIPELINE_STATE_DESC& sourceDescription)
	{
		if (!original)
			return false;

		originalPipelineState = original;
		if (FAILED(original->GetDevice(IID_PPV_ARGS(&device))) || !device)
			return false;

		description = sourceDescription;
		rootSignature = sourceDescription.pRootSignature;
		CopyBytecode(sourceDescription.VS, vertexShaderBytecode);
		CopyBytecode(sourceDescription.PS, pixelShaderBytecode);
		CopyBytecode(sourceDescription.GS, geometryShaderBytecode);
		CopyBytecode(sourceDescription.HS, hullShaderBytecode);
		CopyBytecode(sourceDescription.DS, domainShaderBytecode);
		description.VS = MakeBytecode(vertexShaderBytecode);
		description.PS = MakeBytecode(pixelShaderBytecode);
		description.GS = MakeBytecode(geometryShaderBytecode);
		description.HS = MakeBytecode(hullShaderBytecode);
		description.DS = MakeBytecode(domainShaderBytecode);
		CopyInputLayout(sourceDescription.InputLayout, *this);
		CopyStreamOutput(sourceDescription.StreamOutput, *this);
		description.pRootSignature = rootSignature.Get();
		description.CachedPSO = {};
		return true;
	}

	bool OwnedComputePipelineDesc::Initialize(
		ID3D12PipelineState* original,
		const D3D12_COMPUTE_PIPELINE_STATE_DESC& sourceDescription)
	{
		if (!original)
			return false;

		originalPipelineState = original;
		if (FAILED(original->GetDevice(IID_PPV_ARGS(&device))) || !device)
			return false;

		description = sourceDescription;
		rootSignature = sourceDescription.pRootSignature;
		CopyBytecode(sourceDescription.CS, computeShaderBytecode);
		description.CS = MakeBytecode(computeShaderBytecode);
		description.pRootSignature = rootSignature.Get();
		description.CachedPSO = {};
		return true;
	}

	bool OwnedStreamPipelineDesc::Initialize(
		ID3D12PipelineState* original,
		ID3D12RootSignature* selectedRootSignature,
		const std::vector<uint8_t>& sourceStreamBlob,
		const std::vector<uint8_t>& replacementBytecode,
		D3D12_PIPELINE_STATE_SUBOBJECT_TYPE replacementType,
		const std::vector<uint8_t>& vertexBytecode,
		const std::vector<uint8_t>& pixelBytecode,
		const std::vector<uint8_t>& geometryBytecode,
		const std::vector<uint8_t>& hullBytecode,
		const std::vector<uint8_t>& domainBytecode,
		const std::vector<uint8_t>& computeBytecode,
		const std::vector<uint8_t>& amplificationBytecode,
		const std::vector<uint8_t>& meshBytecode,
		const std::vector<D3D12_INPUT_ELEMENT_DESC>& sourceInputElements,
		const std::vector<std::string>& sourceInputSemanticNames,
		const std::vector<D3D12_SO_DECLARATION_ENTRY>& sourceStreamOutputDeclarations,
		const std::vector<std::string>& sourceStreamOutputSemanticNames,
		const std::vector<UINT>& sourceStreamOutputStrides,
		const std::vector<D3D12_VIEW_INSTANCE_LOCATION>& sourceViewInstanceLocations)
	{
		if (!original || sourceStreamBlob.empty())
			return false;

		originalPipelineState = original;
		if (FAILED(original->GetDevice(IID_PPV_ARGS(&device))) || !device)
			return false;

		streamBlob = sourceStreamBlob;
		vertexShaderBytecode = vertexBytecode;
		pixelShaderBytecode = pixelBytecode;
		geometryShaderBytecode = geometryBytecode;
		hullShaderBytecode = hullBytecode;
		domainShaderBytecode = domainBytecode;
		computeShaderBytecode = computeBytecode;
		amplificationShaderBytecode = amplificationBytecode;
		meshShaderBytecode = meshBytecode;
		if (replacementType == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS)
			vertexShaderBytecode = replacementBytecode;
		else if (replacementType == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS)
			pixelShaderBytecode = replacementBytecode;
		else if (replacementType == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS)
			geometryShaderBytecode = replacementBytecode;
		else if (replacementType == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS)
			hullShaderBytecode = replacementBytecode;
		else if (replacementType == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS)
			domainShaderBytecode = replacementBytecode;
		else if (replacementType == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS)
			computeShaderBytecode = replacementBytecode;
		else if (replacementType == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS)
			amplificationShaderBytecode = replacementBytecode;
		else if (replacementType == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS)
			meshShaderBytecode = replacementBytecode;

		inputElements = sourceInputElements;
		if (sourceInputSemanticNames.size() == inputElements.size())
			inputElementSemanticNames = sourceInputSemanticNames;
		else
		{
			inputElementSemanticNames.clear();
			inputElementSemanticNames.reserve(inputElements.size());

			for (const D3D12_INPUT_ELEMENT_DESC& element : inputElements)
			{
				if (element.SemanticName)
					inputElementSemanticNames.push_back(element.SemanticName);
				else
					inputElementSemanticNames.emplace_back();
			}
		}

		streamOutputDeclarations = sourceStreamOutputDeclarations;
		if (sourceStreamOutputSemanticNames.size() == streamOutputDeclarations.size())
			streamOutputSemanticNames = sourceStreamOutputSemanticNames;
		else
		{
			streamOutputSemanticNames.clear();
			streamOutputSemanticNames.reserve(streamOutputDeclarations.size());

			for (const D3D12_SO_DECLARATION_ENTRY& declaration : streamOutputDeclarations)
			{
				if (declaration.SemanticName)
					streamOutputSemanticNames.push_back(declaration.SemanticName);
				else
					streamOutputSemanticNames.emplace_back();
			}
		}
		streamOutputStrides = sourceStreamOutputStrides;
		viewInstanceLocations = sourceViewInstanceLocations;
		rootSignature = selectedRootSignature;

		for (size_t index = 0; index < inputElements.size(); ++index)
		{
			if (inputElementSemanticNames[index].empty())
				inputElements[index].SemanticName = nullptr;
			else
				inputElements[index].SemanticName = inputElementSemanticNames[index].c_str();
		}

		for (size_t index = 0; index < streamOutputDeclarations.size(); ++index)
		{
			if (streamOutputSemanticNames[index].empty())
				streamOutputDeclarations[index].SemanticName = nullptr;
			else
				streamOutputDeclarations[index].SemanticName = streamOutputSemanticNames[index].c_str();
		}

		uint8_t* streamCursor = streamBlob.data();
		uint8_t* streamEnd = streamCursor + streamBlob.size();
		bool foundRootSignature = false;
		bool foundReplacement = false;

		while (streamCursor < streamEnd)
		{
			if (streamCursor + sizeof(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE) > streamEnd)
				return false;

			const auto type = *reinterpret_cast<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE*>(streamCursor);
			const UINT typeIndex = static_cast<UINT>(type);

			if (typeIndex >= ARRAYSIZE(HookD3D12::subobjectSizes) || HookD3D12::subobjectSizes[typeIndex] == 0)
				return false;

			const size_t subobjectSize = HookD3D12::subobjectSizes[typeIndex];

			if (streamCursor + subobjectSize > streamEnd)
				return false;

			void* payload = streamCursor + sizeof(void*);
			D3D12_SHADER_BYTECODE* shaderBytecode = nullptr;
			const std::vector<uint8_t>* sourceBytecode = nullptr;

			switch (type)
			{
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS: shaderBytecode = static_cast<D3D12_SHADER_BYTECODE*>(payload); sourceBytecode = &vertexShaderBytecode; break;
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS: shaderBytecode = static_cast<D3D12_SHADER_BYTECODE*>(payload); sourceBytecode = &pixelShaderBytecode; break;
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS: shaderBytecode = static_cast<D3D12_SHADER_BYTECODE*>(payload); sourceBytecode = &geometryShaderBytecode; break;
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS: shaderBytecode = static_cast<D3D12_SHADER_BYTECODE*>(payload); sourceBytecode = &hullShaderBytecode; break;
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS: shaderBytecode = static_cast<D3D12_SHADER_BYTECODE*>(payload); sourceBytecode = &domainShaderBytecode; break;
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS: shaderBytecode = static_cast<D3D12_SHADER_BYTECODE*>(payload); sourceBytecode = &computeShaderBytecode; break;
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS: shaderBytecode = static_cast<D3D12_SHADER_BYTECODE*>(payload); sourceBytecode = &amplificationShaderBytecode; break;
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS: shaderBytecode = static_cast<D3D12_SHADER_BYTECODE*>(payload); sourceBytecode = &meshShaderBytecode; break;
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE:
					if (!rootSignature)
						rootSignature = *static_cast<ID3D12RootSignature**>(payload);
					*static_cast<ID3D12RootSignature**>(payload) = rootSignature.Get();
					foundRootSignature = true;
					break;
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CACHED_PSO:
					*static_cast<D3D12_CACHED_PIPELINE_STATE*>(payload) = {};
					break;
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT:
					*static_cast<D3D12_INPUT_LAYOUT_DESC*>(payload) = {DataOrNull(inputElements), static_cast<UINT>(inputElements.size())};
					break;
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT:
				{
					auto* output = static_cast<D3D12_STREAM_OUTPUT_DESC*>(payload);
					output->pSODeclaration = DataOrNull(streamOutputDeclarations);
					output->NumEntries = static_cast<UINT>(streamOutputDeclarations.size());
					output->pBufferStrides = DataOrNull(streamOutputStrides);
					output->NumStrides = static_cast<UINT>(streamOutputStrides.size());
					break;
				}
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VIEW_INSTANCING:
				{
					auto* viewInstancing = static_cast<D3D12_VIEW_INSTANCING_DESC*>(payload);
					viewInstancing->ViewInstanceCount = static_cast<UINT>(viewInstanceLocations.size());
					viewInstancing->pViewInstanceLocations = DataOrNull(viewInstanceLocations);
					break;
				}
				default:
					break;
			}

			if (shaderBytecode)
			{
				if (type == replacementType)
					foundReplacement = true;
				*shaderBytecode = MakeBytecode(*sourceBytecode);
			}

			streamCursor += subobjectSize;
		}

		if ((replacementType != D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MAX_VALID && !foundReplacement) || !foundRootSignature)
			return false;

		description = {streamBlob.size(), streamBlob.data()};
		return true;
	}

	bool Request(
		const std::string& key,
		NativeCreateCallback createCallback,
		bool urgent,
		bool* outQueued,
		bool queueHookApplyOnCompletion)
	{
		std::shared_ptr<Ticket> ticket;
		bool shouldQueue = false;
		bool shouldPromote = false;
		bool capacityAvailable = true;
		if (outQueued)
			*outQueued = false;

		{
			std::lock_guard<std::mutex> ticketLock(gTicketMutex);
			const auto existing = gTickets.find(key);

			if (existing != gTickets.end())
			{
				ticket = existing->second;
				ticket->lastRequestedTick = GetTickCount64();
				shouldPromote = urgent && !ticket->urgentRequestQueued.exchange(true) && !ticket->started.load();
				shouldQueue = !ticket->queued.load() && !ticket->started.load();
			}
			else
			{
				if (gTickets.size() >= maximumPendingTicketCount)
					capacityAvailable = false;
				else
				{
					ticket = std::make_shared<Ticket>();
					ticket->createCallback = std::move(createCallback);
					ticket->lastRequestedTick = GetTickCount64();
					ticket->urgentRequestQueued = urgent;
					ticket->queueHookApplyOnCompletion = queueHookApplyOnCompletion;
					gTickets.emplace(key, ticket);
					shouldQueue = true;
					if (outQueued)
						*outQueued = true;
				}
			}
		}

		if (!capacityAvailable)
		{
			ScheduleCleanup();
			return false;
		}

		if (shouldPromote)
			EnqueueTicket(ticket, urgent, true);

		if (shouldQueue && !shouldPromote)
			EnqueueTicket(ticket, urgent);

		return true;
	}

	bool Promote(const std::string& key)
	{
		std::shared_ptr<Ticket> ticket;
		{
			std::lock_guard<std::mutex> ticketLock(gTicketMutex);
			const auto ticketEntry = gTickets.find(key);
			if (ticketEntry == gTickets.end())
				return false;
			ticket = ticketEntry->second;
		}

		if (ticket->started.load() || ticket->urgentRequestQueued.exchange(true))
			return false;

		return EnqueueTicket(ticket, true, true);
	}

	void QueueReleaseOnWorker(Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState)
	{
		if (!pipelineState)
			return;

		DatabaseModifiedShaders::QueueBackgroundCompile(
			[pipelineState = std::move(pipelineState)]() mutable
			{
				pipelineState.Reset();
			});
	}

	TicketResult Poll(const std::string& key)
	{
		TicketResult result{};
		std::lock_guard<std::mutex> ticketLock(gTicketMutex);
		const auto ticketEntry = gTickets.find(key);

		if (ticketEntry == gTickets.end())
		{
			result.status = TicketStatus::Unavailable;
			result.result = E_PENDING;
			return result;
		}

		const std::shared_ptr<Ticket> ticket = ticketEntry->second;
		ticket->lastRequestedTick = GetTickCount64();
		std::lock_guard<std::mutex> stateLock(ticket->mutex);

		if (ticket->state == State::Pending)
			return result;

		result.result = ticket->result;

		if (ticket->state == State::Ready)
		{
			result.status = TicketStatus::Ready;
			result.pipelineState = std::move(ticket->pipelineState);
		}
		else
			result.status = TicketStatus::Failed;

		gTickets.erase(ticketEntry);
		return result;
	}

	void ReleaseResources()
	{
		std::vector<Microsoft::WRL::ComPtr<ID3D12PipelineState>> retainedPipelineStates;
		std::vector<NativeCreateCallback> retainedCallbacks;

		{
			std::lock_guard<std::mutex> ticketLock(gTicketMutex);

			for (auto& ticketEntry : gTickets)
			{
				std::lock_guard<std::mutex> stateLock(ticketEntry.second->mutex);
				retainedPipelineStates.push_back(std::move(ticketEntry.second->pipelineState));
				retainedCallbacks.push_back(std::move(ticketEntry.second->createCallback));
			}

			gTickets.clear();
			gCleanupQueued = false;
		}

		if (retainedPipelineStates.empty() && retainedCallbacks.empty())
			return;

		DatabaseModifiedShaders::QueueBackgroundCompile(
			[retainedPipelineStates = std::move(retainedPipelineStates), retainedCallbacks = std::move(retainedCallbacks)]() mutable
			{
				retainedPipelineStates.clear();
				retainedCallbacks.clear();
			});
	}
}
