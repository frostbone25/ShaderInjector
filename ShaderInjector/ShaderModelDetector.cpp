#include "ShaderModelDetector.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>

namespace ShaderModelDetector
{
	namespace
	{
		constexpr uint32_t MakeFourCharacterCode(char a, char b, char c, char d)
		{
			return static_cast<uint32_t>(static_cast<uint8_t>(a)) |
				(static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8) |
				(static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16) |
				(static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24);
		}

		constexpr uint32_t directXBytecodeContainerFourCharacterCode = MakeFourCharacterCode('D', 'X', 'B', 'C');
		constexpr uint32_t directXIntermediateLanguageFourCharacterCode = MakeFourCharacterCode('D', 'X', 'I', 'L');
		constexpr uint32_t shaderCodeFourCharacterCode = MakeFourCharacterCode('S', 'H', 'D', 'R');
		constexpr uint32_t extendedShaderCodeFourCharacterCode = MakeFourCharacterCode('S', 'H', 'E', 'X');

		constexpr size_t shaderStageCount = static_cast<size_t>(ShaderTarget::Unknown);
		constexpr std::array<Globals::ShaderModel, 9> supportedShaderModels =
		{
			Globals::ShaderModel::ShaderModel5_0,
			Globals::ShaderModel::ShaderModel5_1,
			Globals::ShaderModel::ShaderModel6_0,
			Globals::ShaderModel::ShaderModel6_1,
			Globals::ShaderModel::ShaderModel6_2,
			Globals::ShaderModel::ShaderModel6_3,
			Globals::ShaderModel::ShaderModel6_4,
			Globals::ShaderModel::ShaderModel6_5,
			Globals::ShaderModel::ShaderModel6_6,
		};

		struct StageObservations
		{
			std::array<std::atomic<uint32_t>, supportedShaderModels.size()> modelCounts{};
		};

		std::array<StageObservations, shaderStageCount> stageObservations{};

		bool ReadUInt32(const uint8_t* bytes, size_t byteCount, size_t offset, uint32_t& value)
		{
			if (!bytes || offset > byteCount || byteCount - offset < sizeof(uint32_t))
				return false;

			std::memcpy(&value, bytes + offset, sizeof(value));
			return true;
		}

		bool TryDecodeShaderVersionToken(
			uint32_t shaderVersionToken,
			ShaderTarget::ShaderType expectedShaderType,
			Globals::ShaderModel& shaderModel)
		{
			const uint32_t encodedShaderType = (shaderVersionToken >> 16) & 0xffffu;
			ShaderTarget::ShaderType decodedShaderType = ShaderTarget::Unknown;

			switch (encodedShaderType)
			{
				case 0: decodedShaderType = ShaderTarget::PixelShader; break;
				case 1: decodedShaderType = ShaderTarget::VertexShader; break;
				case 2: decodedShaderType = ShaderTarget::GeometryShader; break;
				case 3: decodedShaderType = ShaderTarget::HullShader; break;
				case 4: decodedShaderType = ShaderTarget::DomainShader; break;
				case 5: decodedShaderType = ShaderTarget::ComputeShader; break;
				default: return false;
			}

			if (decodedShaderType != expectedShaderType)
				return false;

			const uint32_t majorVersion = (shaderVersionToken >> 4) & 0x0fu;
			const uint32_t minorVersion = shaderVersionToken & 0x0fu;
			const int encodedShaderModel = static_cast<int>(majorVersion * 10u + minorVersion);

			for (Globals::ShaderModel supportedShaderModel : supportedShaderModels)
			{
				if (static_cast<int>(supportedShaderModel) == encodedShaderModel)
				{
					shaderModel = supportedShaderModel;
					return true;
				}
			}

			return false;
		}

		bool TryDetectShaderModel(
			ShaderTarget::ShaderType expectedShaderType,
			const void* shaderBytecode,
			size_t shaderBytecodeSize,
			Globals::ShaderModel& shaderModel)
		{
			if (!shaderBytecode || shaderBytecodeSize < sizeof(uint32_t))
				return false;

			const auto* bytes = static_cast<const uint8_t*>(shaderBytecode);
			uint32_t containerFourCharacterCode = 0;
			if (!ReadUInt32(bytes, shaderBytecodeSize, 0, containerFourCharacterCode))
				return false;

			// D3DCompiler and DXC both emit a DXBC container. The container's chunk
			// table points to either legacy SHDR/SHEX tokens or a DXIL program header.
			if (containerFourCharacterCode == directXBytecodeContainerFourCharacterCode)
			{
				constexpr size_t chunkCountOffset = 28;
				constexpr size_t chunkOffsetsOffset = 32;
				uint32_t chunkCount = 0;
				if (!ReadUInt32(bytes, shaderBytecodeSize, chunkCountOffset, chunkCount))
					return false;

				const size_t maximumChunkCount = shaderBytecodeSize >= chunkOffsetsOffset
					? (shaderBytecodeSize - chunkOffsetsOffset) / sizeof(uint32_t)
					: 0;
				if (chunkCount > maximumChunkCount)
					return false;

				for (uint32_t chunkIndex = 0; chunkIndex < chunkCount; ++chunkIndex)
				{
					uint32_t chunkOffset = 0;
					if (!ReadUInt32(bytes, shaderBytecodeSize, chunkOffsetsOffset + chunkIndex * sizeof(uint32_t), chunkOffset))
						return false;

					uint32_t chunkFourCharacterCode = 0;
					uint32_t chunkSize = 0;
					if (!ReadUInt32(bytes, shaderBytecodeSize, chunkOffset, chunkFourCharacterCode) ||
						!ReadUInt32(bytes, shaderBytecodeSize, static_cast<size_t>(chunkOffset) + sizeof(uint32_t), chunkSize))
					{
						continue;
					}

					constexpr size_t chunkHeaderSize = sizeof(uint32_t) * 2;
					const size_t chunkDataOffset = static_cast<size_t>(chunkOffset) + chunkHeaderSize;
					if (chunkSize < sizeof(uint32_t) || chunkDataOffset > shaderBytecodeSize ||
						static_cast<size_t>(chunkSize) > shaderBytecodeSize - chunkDataOffset)
					{
						continue;
					}

					if (chunkFourCharacterCode != directXIntermediateLanguageFourCharacterCode &&
						chunkFourCharacterCode != shaderCodeFourCharacterCode &&
						chunkFourCharacterCode != extendedShaderCodeFourCharacterCode)
					{
						continue;
					}

					uint32_t shaderVersionToken = 0;
					if (ReadUInt32(bytes, shaderBytecodeSize, chunkDataOffset, shaderVersionToken) &&
						TryDecodeShaderVersionToken(shaderVersionToken, expectedShaderType, shaderModel))
					{
						return true;
					}
				}

				return false;
			}

			// Retain support for a raw legacy token stream even though D3D12 games
			// normally submit a complete DXBC container.
			return TryDecodeShaderVersionToken(containerFourCharacterCode, expectedShaderType, shaderModel);
		}

		size_t FindShaderModelIndex(Globals::ShaderModel shaderModel)
		{
			for (size_t index = 0; index < supportedShaderModels.size(); ++index)
			{
				if (supportedShaderModels[index] == shaderModel)
					return index;
			}

			return supportedShaderModels.size();
		}
	}

	void ObserveShaderBytecode(
		ShaderTarget::ShaderType expectedShaderType,
		const void* shaderBytecode,
		size_t shaderBytecodeSize)
	{
		const size_t stageIndex = static_cast<size_t>(expectedShaderType);
		if (stageIndex >= stageObservations.size())
			return;

		Globals::ShaderModel detectedShaderModel = Globals::ShaderModel::ShaderModel6_6;
		if (!TryDetectShaderModel(expectedShaderType, shaderBytecode, shaderBytecodeSize, detectedShaderModel))
			return;

		const size_t modelIndex = FindShaderModelIndex(detectedShaderModel);
		if (modelIndex < supportedShaderModels.size())
			stageObservations[stageIndex].modelCounts[modelIndex].fetch_add(1, std::memory_order_relaxed);
	}

	bool TryGetDetectedShaderModel(
		ShaderTarget::ShaderType shaderType,
		Globals::ShaderModel& detectedShaderModel)
	{
		const size_t stageIndex = static_cast<size_t>(shaderType);
		if (stageIndex >= stageObservations.size())
			return false;

		uint32_t highestObservationCount = 0;
		size_t mostFrequentModelIndex = supportedShaderModels.size();
		for (size_t modelIndex = 0; modelIndex < supportedShaderModels.size(); ++modelIndex)
		{
			const uint32_t observationCount = stageObservations[stageIndex].modelCounts[modelIndex].load(std::memory_order_relaxed);
			if (observationCount > highestObservationCount)
			{
				highestObservationCount = observationCount;
				mostFrequentModelIndex = modelIndex;
			}
		}

		if (mostFrequentModelIndex >= supportedShaderModels.size())
			return false;

		detectedShaderModel = supportedShaderModels[mostFrequentModelIndex];
		return true;
	}

	Globals::ShaderModel GetEffectiveShaderModel(
		ShaderTarget::ShaderType shaderType,
		Globals::ShaderModel configuredShaderModel)
	{
		if (!Globals::gAutoDetectShaderModels)
			return configuredShaderModel;

		Globals::ShaderModel detectedShaderModel = configuredShaderModel;
		return TryGetDetectedShaderModel(shaderType, detectedShaderModel)
			? detectedShaderModel
			: configuredShaderModel;
	}
}
