#include "D3D12Pipeline.h"
#include <vector>
#include "D3D12Exception.h"
#include "DXCompiler.h"

static D3D12_SHADER_VISIBILITY MapShaderVisibility(ShaderVisibility visibility)
{
	// Map to D3D12 visibility - be conservative if multiple stages
	uint32_t visFlags = static_cast<uint32_t>(visibility);

	if (visFlags == static_cast<uint32_t>(ShaderVisibility::ALL))
		return D3D12_SHADER_VISIBILITY_ALL;

	// Check for single stage visibility
	if (HasFlag(visibility, ShaderVisibility::VERTEX_SHADER) &&
		(visFlags & ~static_cast<uint32_t>(ShaderVisibility::VERTEX_SHADER)) == 0)
		return D3D12_SHADER_VISIBILITY_VERTEX;
	if (HasFlag(visibility, ShaderVisibility::PIXEL_SHADER) &&
		(visFlags & ~static_cast<uint32_t>(ShaderVisibility::PIXEL_SHADER)) == 0)
		return D3D12_SHADER_VISIBILITY_PIXEL;
	if (HasFlag(visibility, ShaderVisibility::GEOMETRY_SHADER) &&
		(visFlags & ~static_cast<uint32_t>(ShaderVisibility::GEOMETRY_SHADER)) == 0)
		return D3D12_SHADER_VISIBILITY_GEOMETRY;
	if (HasFlag(visibility, ShaderVisibility::HULL_SHADER) &&
		(visFlags & ~static_cast<uint32_t>(ShaderVisibility::HULL_SHADER)) == 0)
		return D3D12_SHADER_VISIBILITY_HULL;
	if (HasFlag(visibility, ShaderVisibility::DOMAIN_SHADER) &&
		(visFlags & ~static_cast<uint32_t>(ShaderVisibility::DOMAIN_SHADER)) == 0)
		return D3D12_SHADER_VISIBILITY_DOMAIN;
	if (HasFlag(visibility, ShaderVisibility::MESH_SHADER) &&
		(visFlags & ~static_cast<uint32_t>(ShaderVisibility::MESH_SHADER)) == 0)
		return D3D12_SHADER_VISIBILITY_MESH;
	if (HasFlag(visibility, ShaderVisibility::AMPLIFICATION_SHADER) &&
		(visFlags & ~static_cast<uint32_t>(ShaderVisibility::AMPLIFICATION_SHADER)) == 0)
		return D3D12_SHADER_VISIBILITY_AMPLIFICATION;

	// Multiple stages - use ALL
	return D3D12_SHADER_VISIBILITY_ALL;
}

static D3D12_DESCRIPTOR_RANGE_TYPE MapDescriptorRangeType(DescriptorRangeType type)
{
	switch (type)
	{
		case DescriptorRangeType::CBV:
			return D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
		case DescriptorRangeType::SRV_BUFFER:
		case DescriptorRangeType::SRV_TEXTURE:
			return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		case DescriptorRangeType::UAV_BUFFER:
		case DescriptorRangeType::UAV_TEXTURE:
			return D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
		case DescriptorRangeType::SAMPLER:
			return D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
		default:
			return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	}
}

static D3D12_PRIMITIVE_TOPOLOGY_TYPE MapPrimitiveTopologyType(PrimitiveTopology topology)
{
	switch (topology)
	{
		case PrimitiveTopology::POINT_LIST:
			return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
		case PrimitiveTopology::LINE_LIST:
		case PrimitiveTopology::LINE_STRIP:
		case PrimitiveTopology::LINE_LIST_ADJ:
		case PrimitiveTopology::LINE_STRIP_ADJ:
			return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
		case PrimitiveTopology::TRIANGLE_LIST:
		case PrimitiveTopology::TRIANGLE_STRIP:
		case PrimitiveTopology::TRIANGLE_LIST_ADJ:
		case PrimitiveTopology::TRIANGLE_STRIP_ADJ:
			return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		case PrimitiveTopology::PATCH_LIST:
			return D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
		default:
			return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	}
}

D3D12GraphicsPipeline &D3D12GraphicsPipeline::BuildRootSignature(const PipelineLayoutCreateInfo &createInfo)
{
	std::vector<CD3DX12_ROOT_PARAMETER1> rootParameters;
	std::vector<std::vector<CD3DX12_DESCRIPTOR_RANGE1>> descriptorRangesStorage;
	std::vector<CD3DX12_STATIC_SAMPLER_DESC> staticSamplers;

	// Process each binding
	for (const auto &binding: createInfo.bindings)
	{
		CD3DX12_ROOT_PARAMETER1 rootParam;

		switch (binding.type)
		{
			case DescriptorParameterType::Constants:
			{
				rootParam.InitAsConstants(binding.constants.num32BitValues,
										  0,
										  0,
										  MapShaderVisibility(binding.constants.visibility));
				rootParameters.push_back(rootParam);
				break;
			}
			case DescriptorParameterType::CBV:
			{
				rootParam.InitAsConstantBufferView(binding.descriptor.shaderRegister,
												   binding.descriptor.registerSpace,
												   D3D12_ROOT_DESCRIPTOR_FLAG_NONE,
												   MapShaderVisibility(binding.descriptor.visibility));
				rootParameters.push_back(rootParam);
				break;
			}
			case DescriptorParameterType::SRV:
			{
				rootParam.InitAsShaderResourceView(binding.descriptor.shaderRegister,
												   binding.descriptor.registerSpace,
												   D3D12_ROOT_DESCRIPTOR_FLAG_NONE,
												   MapShaderVisibility(binding.descriptor.visibility));
				rootParameters.push_back(rootParam);
				break;
			}
			case DescriptorParameterType::UAV:
			{
				rootParam.InitAsUnorderedAccessView(binding.descriptor.shaderRegister,
													binding.descriptor.registerSpace,
													D3D12_ROOT_DESCRIPTOR_FLAG_NONE,
													MapShaderVisibility(binding.descriptor.visibility));
				rootParameters.push_back(rootParam);
				break;
			}
			case DescriptorParameterType::DescriptorTable:
			{
				std::vector<CD3DX12_DESCRIPTOR_RANGE1> ranges;
				for (const auto &range: binding.descriptorTable.ranges)
				{
					CD3DX12_DESCRIPTOR_RANGE1 d3d12Range;
					// Use DESCRIPTORS_VOLATILE flag for large or unbounded arrays
					// This is required for arrays with many descriptors (>1000) to avoid validation warnings
					D3D12_DESCRIPTOR_RANGE_FLAGS flags = (range.numDescriptors > 1000)
																 ? D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE
																 : D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
					d3d12Range.Init(MapDescriptorRangeType(range.rangeType),
									range.numDescriptors,
									range.baseShaderRegister,
									range.registerSpace,
									flags,
									range.offsetInDescriptorsFromTableStart);
					ranges.push_back(d3d12Range);
				}
				descriptorRangesStorage.push_back(ranges);
				rootParam.InitAsDescriptorTable(static_cast<UINT>(descriptorRangesStorage.back().size()),
												descriptorRangesStorage.back().data(),
												MapShaderVisibility(binding.descriptorTable.visibility));
				rootParameters.push_back(rootParam);
				break;
			}
		}
	}

	// Process static samplers
	for (size_t i = 0; i < createInfo.staticSamplers.size(); ++i)
	{
		const auto &sampler = createInfo.staticSamplers[i];

		D3D12_FILTER filter;
		switch (sampler.filter)
		{
			case Filter::POINT:
				filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
				break;
			case Filter::LINEAR:
				filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
				break;
			case Filter::ANISOTROPIC:
				filter = D3D12_FILTER_ANISOTROPIC;
				break;
			default:
				filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		}

		auto ConvertAddressMode = [](AddressMode mode) -> D3D12_TEXTURE_ADDRESS_MODE
		{
			switch (mode)
			{
				case AddressMode::WRAP:
					return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
				case AddressMode::CLAMP:
					return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
				case AddressMode::BORDER:
					return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
				default:
					return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			}
		};

		CD3DX12_STATIC_SAMPLER_DESC samplerDesc(static_cast<UINT>(i),
												filter,
												ConvertAddressMode(sampler.addressU),
												ConvertAddressMode(sampler.addressV),
												ConvertAddressMode(sampler.addressW),
												sampler.mipLODBias,
												sampler.maxAnisotropy,
												D3D12_COMPARISON_FUNC_NEVER,
												D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK,
												sampler.minLOD,
												sampler.maxLOD,
												MapShaderVisibility(sampler.visibility));

		staticSamplers.push_back(samplerDesc);
	}

	// Create root signature
	D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
	rootSignatureDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
	rootSignatureDesc.Desc_1_1.NumParameters = static_cast<UINT>(rootParameters.size());
	rootSignatureDesc.Desc_1_1.pParameters = rootParameters.empty() ? nullptr : rootParameters.data();
	rootSignatureDesc.Desc_1_1.NumStaticSamplers = static_cast<UINT>(staticSamplers.size());
	rootSignatureDesc.Desc_1_1.pStaticSamplers = staticSamplers.empty() ? nullptr : staticSamplers.data();

	rootSignatureDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
									   D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
									   D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED;

	Microsoft::WRL::ComPtr<ID3DBlob> signature;
	Microsoft::WRL::ComPtr<ID3DBlob> error;
	HRESULT hr = D3D12SerializeVersionedRootSignature(&rootSignatureDesc, &signature, &error);
	if (FAILED(hr))
	{
		if (error)
		{
			const char *errorMsg = static_cast<const char *>(error->GetBufferPointer());
			throw D3D12Exception(errorMsg, hr);
		}
		throw D3D12Exception("Failed to serialize root signature", hr);
	}

	hr = device_->CreateRootSignature(0,
									  signature->GetBufferPointer(),
									  signature->GetBufferSize(),
									  IID_PPV_ARGS(&rootSignature_));
	if (FAILED(hr))
	{
		throw D3D12Exception("Failed to create root signature", hr);
	}

	return *this;
}

D3D12GraphicsPipeline &D3D12GraphicsPipeline::BuildRootSignatureFromShader(const ShaderCompilationResult &compileResult)
{
	if (!compileResult.rootSignatureBlob || compileResult.rootSignatureBlob->GetBufferSize() == 0)
	{
		throw D3D12Exception("No root signature found in shader. Make sure to define 'rootSig' in the shader.", E_FAIL);
	}

	HRESULT hr = device_->CreateRootSignature(0,
												  compileResult.rootSignatureBlob->GetBufferPointer(),
												  compileResult.rootSignatureBlob->GetBufferSize(),
												  IID_PPV_ARGS(&rootSignature_));
	if (FAILED(hr))
	{
		throw D3D12Exception("Failed to create root signature from shader", hr);
	}

	return *this;
}

void D3D12GraphicsPipeline::BuildGraphicsPipeline(const GraphicsPipelineCreateInfo &pipelineInfo,
												  const ShaderCompilationResult &compileResult)
{
	if (!rootSignature_)
	{
		throw D3D12Exception("Root signature must be built before pipeline", E_FAIL);
	}

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = rootSignature_.Get();

	// Check for vertex shader
	auto vsIt = compileResult.compiledBlobs.find("vertexMain");
	if (vsIt == compileResult.compiledBlobs.end())
	{
		vsIt = compileResult.compiledBlobs.find("VSMain");
	}
	if (vsIt == compileResult.compiledBlobs.end())
	{
		vsIt = compileResult.compiledBlobs.find("main");
	}

	if (vsIt != compileResult.compiledBlobs.end())
	{
		// Traditional vertex + pixel pipeline
		psoDesc.VS = { vsIt->second->GetBufferPointer(), vsIt->second->GetBufferSize() };

		// Check for pixel shader
		auto psIt = compileResult.compiledBlobs.find("pixelMain");
		if (psIt == compileResult.compiledBlobs.end())
		{
			psIt = compileResult.compiledBlobs.find("PSMain");
		}
		if (psIt == compileResult.compiledBlobs.end())
		{
			psIt = compileResult.compiledBlobs.find("fragmentMain");
		}
		if (psIt != compileResult.compiledBlobs.end())
		{
			psoDesc.PS = { psIt->second->GetBufferPointer(), psIt->second->GetBufferSize() };
		}

		// Input layout for vertex shader
		std::vector<D3D12_INPUT_ELEMENT_DESC> inputLayout;

		if (!pipelineInfo.inputElements.empty())
		{
			for (const auto &elem: pipelineInfo.inputElements)
			{
				D3D12_INPUT_ELEMENT_DESC desc = {};
				desc.SemanticName = elem.semanticName;
				desc.SemanticIndex = elem.semanticIndex;
				desc.Format = elem.format;
				desc.InputSlot = elem.inputSlot;
				desc.AlignedByteOffset = elem.alignedByteOffset;
				desc.InputSlotClass = elem.inputSlotClass;
				desc.InstanceDataStepRate = elem.instanceDataStepRate;
				inputLayout.push_back(desc);
			}
			psoDesc.InputLayout = { inputLayout.data(), static_cast<UINT>(inputLayout.size()) };
		}
		else
		{
			psoDesc.InputLayout = { nullptr, 0 };
		}

		// Check for mesh shader
		auto msIt = compileResult.compiledBlobs.find("meshMain");
		if (msIt == compileResult.compiledBlobs.end())
		{
			msIt = compileResult.compiledBlobs.find("MSMain");
		}

		// If mesh shader is present, build mesh shader pipeline instead
		if (msIt != compileResult.compiledBlobs.end())
		{
			BuildMeshShaderPipeline(pipelineInfo, compileResult);
			return;
		}
	}

	// Rasterizer state
	psoDesc.RasterizerState.FillMode = pipelineInfo.rasterizerState.fillMode;
	psoDesc.RasterizerState.CullMode = pipelineInfo.rasterizerState.cullMode;
	psoDesc.RasterizerState.FrontCounterClockwise = pipelineInfo.rasterizerState.frontCounterClockwise;
	psoDesc.RasterizerState.DepthBias = pipelineInfo.rasterizerState.depthBias;
	psoDesc.RasterizerState.DepthBiasClamp = pipelineInfo.rasterizerState.depthBiasClamp;
	psoDesc.RasterizerState.SlopeScaledDepthBias = pipelineInfo.rasterizerState.slopeScaledDepthBias;
	psoDesc.RasterizerState.DepthClipEnable = pipelineInfo.rasterizerState.depthClipEnable;
	psoDesc.RasterizerState.MultisampleEnable = pipelineInfo.rasterizerState.multisampleEnable;
	psoDesc.RasterizerState.AntialiasedLineEnable = pipelineInfo.rasterizerState.antialiasedLineEnable;
	psoDesc.RasterizerState.ForcedSampleCount = 0;
	psoDesc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

	// Depth stencil state
	psoDesc.DepthStencilState.DepthEnable = pipelineInfo.depthStencilState.depthEnable;
	psoDesc.DepthStencilState.DepthWriteMask = pipelineInfo.depthStencilState.depthWriteMask;
	psoDesc.DepthStencilState.DepthFunc = pipelineInfo.depthStencilState.depthFunc;
	psoDesc.DepthStencilState.StencilEnable = pipelineInfo.depthStencilState.stencilEnable;
	psoDesc.DepthStencilState.StencilReadMask = pipelineInfo.depthStencilState.stencilReadMask;
	psoDesc.DepthStencilState.StencilWriteMask = pipelineInfo.depthStencilState.stencilWriteMask;

	// Blend state
	psoDesc.BlendState.AlphaToCoverageEnable = pipelineInfo.blendState.alphaToCoverageEnable;
	if (!pipelineInfo.blendState.rtBlends.empty())
	{
		psoDesc.BlendState.IndependentBlendEnable = TRUE;
		for (size_t i = 0; i < pipelineInfo.blendState.rtBlends.size() && i < 8; ++i)
			psoDesc.BlendState.RenderTarget[i] = pipelineInfo.blendState.rtBlends[i];
	}
	else
	{
		psoDesc.BlendState.IndependentBlendEnable = pipelineInfo.blendState.independentBlendEnable;
		psoDesc.BlendState.RenderTarget[0] = pipelineInfo.blendState.renderTargetBlend;
	}

	// Render target formats
	psoDesc.NumRenderTargets = static_cast<UINT>(pipelineInfo.rtvFormats.size());
	for (size_t i = 0; i < pipelineInfo.rtvFormats.size() && i < 8; ++i)
	{
		psoDesc.RTVFormats[i] = pipelineInfo.rtvFormats[i];
	}
	psoDesc.DSVFormat = pipelineInfo.dsvFormat;

	// Sample desc
	psoDesc.SampleDesc.Count = pipelineInfo.sampleCount;
	psoDesc.SampleDesc.Quality = pipelineInfo.sampleQuality;

	// Primitive topology
	psoDesc.PrimitiveTopologyType = MapPrimitiveTopologyType(pipelineInfo.primitiveTopology);

	// Misc
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

	HRESULT hr = device_->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState_));
	if (FAILED(hr))
	{
		throw D3D12Exception("Failed to create graphics pipeline state", hr);
	}
}

// Pipeline state stream structure for mesh shaders
struct MeshShaderPipelineStateStream
{
	CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE RootSignature;
	CD3DX12_PIPELINE_STATE_STREAM_AS AS;
	CD3DX12_PIPELINE_STATE_STREAM_MS MS;
	CD3DX12_PIPELINE_STATE_STREAM_PS PS;
	CD3DX12_PIPELINE_STATE_STREAM_BLEND_DESC BlendState;
	CD3DX12_PIPELINE_STATE_STREAM_SAMPLE_MASK SampleMask;
	CD3DX12_PIPELINE_STATE_STREAM_RASTERIZER RasterizerState;
	CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL DepthStencilState;
	CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
	CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT DSVFormat;
	CD3DX12_PIPELINE_STATE_STREAM_SAMPLE_DESC SampleDesc;
	CD3DX12_PIPELINE_STATE_STREAM_FLAGS Flags;
	CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY PrimitiveTopologyType;
};

void D3D12GraphicsPipeline::BuildMeshShaderPipeline(const GraphicsPipelineCreateInfo &pipelineInfo,
													const ShaderCompilationResult &compileResult)
{
	if (!rootSignature_)
	{
		throw D3D12Exception("Root signature must be built before pipeline", E_FAIL);
	}

	// Verify device supports mesh shaders
	D3D12_FEATURE_DATA_D3D12_OPTIONS7 features = {};
	HRESULT hr = device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &features, sizeof(features));
	if (FAILED(hr) || features.MeshShaderTier == D3D12_MESH_SHADER_TIER_NOT_SUPPORTED)
	{
		throw D3D12Exception("Mesh shaders are not supported on this device", E_NOTIMPL);
	}

	MeshShaderPipelineStateStream psoStream = {};

	// Root signature
	psoStream.RootSignature = rootSignature_.Get();

	// Amplification shader (optional)
	auto asIt = compileResult.compiledBlobs.find("amplificationMain");
	if (asIt == compileResult.compiledBlobs.end())
	{
		asIt = compileResult.compiledBlobs.find("ASMain");
	}
	if (asIt != compileResult.compiledBlobs.end())
	{
		psoStream.AS = CD3DX12_SHADER_BYTECODE(asIt->second->GetBufferPointer(), asIt->second->GetBufferSize());
	}

	// Mesh shader (required)
	auto msIt = compileResult.compiledBlobs.find("meshMain");
	if (msIt == compileResult.compiledBlobs.end())
	{
		msIt = compileResult.compiledBlobs.find("MSMain");
	}
	if (msIt != compileResult.compiledBlobs.end())
	{
		psoStream.MS = CD3DX12_SHADER_BYTECODE(msIt->second->GetBufferPointer(), msIt->second->GetBufferSize());
	}
	else
	{
		throw D3D12Exception("Mesh shader is required for mesh shader pipeline", E_INVALIDARG);
	}

	// Pixel shader (optional but typically present)
	auto psIt = compileResult.compiledBlobs.find("pixelMain");
	if (psIt == compileResult.compiledBlobs.end())
	{
		psIt = compileResult.compiledBlobs.find("PSMain");
	}
	if (psIt == compileResult.compiledBlobs.end())
	{
		psIt = compileResult.compiledBlobs.find("fragmentMain");
	}
	if (psIt != compileResult.compiledBlobs.end())
	{
		psoStream.PS = CD3DX12_SHADER_BYTECODE(psIt->second->GetBufferPointer(), psIt->second->GetBufferSize());
	}

	// Blend state
	D3D12_BLEND_DESC blendDesc = {};
	blendDesc.AlphaToCoverageEnable = pipelineInfo.blendState.alphaToCoverageEnable;
	if (!pipelineInfo.blendState.rtBlends.empty())
	{
		blendDesc.IndependentBlendEnable = TRUE;
		for (size_t i = 0; i < pipelineInfo.blendState.rtBlends.size() && i < 8; ++i)
			blendDesc.RenderTarget[i] = pipelineInfo.blendState.rtBlends[i];
	}
	else
	{
		blendDesc.IndependentBlendEnable = pipelineInfo.blendState.independentBlendEnable;
		blendDesc.RenderTarget[0] = pipelineInfo.blendState.renderTargetBlend;
	}
	psoStream.BlendState = CD3DX12_BLEND_DESC(blendDesc);

	// Sample mask
	psoStream.SampleMask = UINT_MAX;

	// Rasterizer state
	D3D12_RASTERIZER_DESC rasterizerDesc = {};
	rasterizerDesc.FillMode = pipelineInfo.rasterizerState.fillMode;
	rasterizerDesc.CullMode = pipelineInfo.rasterizerState.cullMode;
	rasterizerDesc.FrontCounterClockwise = pipelineInfo.rasterizerState.frontCounterClockwise;
	rasterizerDesc.DepthBias = pipelineInfo.rasterizerState.depthBias;
	rasterizerDesc.DepthBiasClamp = pipelineInfo.rasterizerState.depthBiasClamp;
	rasterizerDesc.SlopeScaledDepthBias = pipelineInfo.rasterizerState.slopeScaledDepthBias;
	rasterizerDesc.DepthClipEnable = pipelineInfo.rasterizerState.depthClipEnable;
	rasterizerDesc.MultisampleEnable = pipelineInfo.rasterizerState.multisampleEnable;
	rasterizerDesc.AntialiasedLineEnable = pipelineInfo.rasterizerState.antialiasedLineEnable;
	rasterizerDesc.ForcedSampleCount = 0;
	rasterizerDesc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
	psoStream.RasterizerState = CD3DX12_RASTERIZER_DESC(rasterizerDesc);

	// Depth stencil state
	D3D12_DEPTH_STENCIL_DESC depthStencilDesc = {};
	depthStencilDesc.DepthEnable = pipelineInfo.depthStencilState.depthEnable;
	depthStencilDesc.DepthWriteMask = pipelineInfo.depthStencilState.depthWriteMask;
	depthStencilDesc.DepthFunc = pipelineInfo.depthStencilState.depthFunc;
	depthStencilDesc.StencilEnable = pipelineInfo.depthStencilState.stencilEnable;
	depthStencilDesc.StencilReadMask = pipelineInfo.depthStencilState.stencilReadMask;
	depthStencilDesc.StencilWriteMask = pipelineInfo.depthStencilState.stencilWriteMask;
	// Front face stencil ops (defaults)
	depthStencilDesc.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	depthStencilDesc.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	depthStencilDesc.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
	depthStencilDesc.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	// Back face stencil ops (defaults)
	depthStencilDesc.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	depthStencilDesc.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	depthStencilDesc.BackFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
	depthStencilDesc.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	psoStream.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(depthStencilDesc);

	// Render target formats
	D3D12_RT_FORMAT_ARRAY rtvFormats = {};
	rtvFormats.NumRenderTargets = static_cast<UINT>(pipelineInfo.rtvFormats.size());
	for (size_t i = 0; i < 8; ++i)
	{
		rtvFormats.RTFormats[i] = DXGI_FORMAT_UNKNOWN;
	}
	// Set the actual formats
	for (size_t i = 0; i < pipelineInfo.rtvFormats.size() && i < 8; ++i)
	{
		rtvFormats.RTFormats[i] = pipelineInfo.rtvFormats[i];
	}
	psoStream.RTVFormats = rtvFormats;

	// Depth stencil format
	psoStream.DSVFormat = pipelineInfo.dsvFormat;

	// Sample desc
	DXGI_SAMPLE_DESC sampleDesc = {};
	sampleDesc.Count = pipelineInfo.sampleCount;
	sampleDesc.Quality = pipelineInfo.sampleQuality;
	psoStream.SampleDesc = sampleDesc;

	psoStream.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
	psoStream.PrimitiveTopologyType = MapPrimitiveTopologyType(pipelineInfo.primitiveTopology);

	// Create pipeline state using stream desc
	D3D12_PIPELINE_STATE_STREAM_DESC streamDesc = {};
	streamDesc.SizeInBytes = sizeof(psoStream);
	streamDesc.pPipelineStateSubobjectStream = &psoStream;

	Microsoft::WRL::ComPtr<ID3D12Device2> device2;
	hr = device_->QueryInterface(IID_PPV_ARGS(&device2));
	if (FAILED(hr))
	{
		throw D3D12Exception("Device does not support ID3D12Device2", hr);
	}

	hr = device2->CreatePipelineState(&streamDesc, IID_PPV_ARGS(&pipelineState_));
	if (FAILED(hr))
	{
		throw D3D12Exception("Failed to create mesh shader pipeline state", hr);
	}
}

D3D12ComputePipeline &D3D12ComputePipeline::BuildRootSignature(const PipelineLayoutCreateInfo &createInfo)
{
	std::vector<CD3DX12_ROOT_PARAMETER1> rootParameters;
	std::vector<std::vector<CD3DX12_DESCRIPTOR_RANGE1>> descriptorRangesStorage;
	std::vector<CD3DX12_STATIC_SAMPLER_DESC> staticSamplers;

	// Process each binding
	for (const auto &binding: createInfo.bindings)
	{
		CD3DX12_ROOT_PARAMETER1 rootParam;

		switch (binding.type)
		{
			case DescriptorParameterType::Constants:
			{
				rootParam.InitAsConstants(binding.constants.num32BitValues,
										  binding.constants.shaderRegister,
										  binding.constants.registerSpace,
										  D3D12_SHADER_VISIBILITY_ALL);
				rootParameters.push_back(rootParam);
				break;
			}

			case DescriptorParameterType::DescriptorTable:
			{
				std::vector<CD3DX12_DESCRIPTOR_RANGE1> descriptorRanges;

				for (const auto &range: binding.descriptorTable.ranges)
				{
					CD3DX12_DESCRIPTOR_RANGE1 d3d12Range;
					d3d12Range.Init(MapDescriptorRangeType(range.rangeType),
									range.numDescriptors,
									range.baseShaderRegister,
									range.registerSpace,
									D3D12_DESCRIPTOR_RANGE_FLAG_NONE,
									range.offsetInDescriptorsFromTableStart);
					descriptorRanges.push_back(d3d12Range);
				}

				descriptorRangesStorage.push_back(descriptorRanges);
				rootParam.InitAsDescriptorTable(static_cast<UINT>(descriptorRanges.size()),
												descriptorRangesStorage.back().data(),
												D3D12_SHADER_VISIBILITY_ALL);
				rootParameters.push_back(rootParam);
				break;
			}

			case DescriptorParameterType::CBV:
			{
				rootParam.InitAsConstantBufferView(binding.descriptor.shaderRegister,
												   binding.descriptor.registerSpace,
												   D3D12_ROOT_DESCRIPTOR_FLAG_NONE,
												   D3D12_SHADER_VISIBILITY_ALL);
				rootParameters.push_back(rootParam);
				break;
			}

			case DescriptorParameterType::SRV:
			{
				rootParam.InitAsShaderResourceView(binding.descriptor.shaderRegister,
												   binding.descriptor.registerSpace,
												   D3D12_ROOT_DESCRIPTOR_FLAG_NONE,
												   D3D12_SHADER_VISIBILITY_ALL);
				rootParameters.push_back(rootParam);
				break;
			}

			case DescriptorParameterType::UAV:
			{
				rootParam.InitAsUnorderedAccessView(binding.descriptor.shaderRegister,
													binding.descriptor.registerSpace,
													D3D12_ROOT_DESCRIPTOR_FLAG_NONE,
													D3D12_SHADER_VISIBILITY_ALL);
				rootParameters.push_back(rootParam);
				break;
			}
		}
	}

	// Process static samplers
	for (size_t i = 0; i < createInfo.staticSamplers.size(); ++i)
	{
		const auto &sampler = createInfo.staticSamplers[i];

		D3D12_FILTER filter;
		switch (sampler.filter)
		{
			case Filter::POINT:
				filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
				break;
			case Filter::LINEAR:
				filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
				break;
			case Filter::ANISOTROPIC:
				filter = D3D12_FILTER_ANISOTROPIC;
				break;
			default:
				filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		}

		auto ConvertAddressMode = [](AddressMode mode) -> D3D12_TEXTURE_ADDRESS_MODE
		{
			switch (mode)
			{
				case AddressMode::WRAP:
					return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
				case AddressMode::CLAMP:
					return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
				case AddressMode::BORDER:
					return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
				default:
					return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			}
		};

		CD3DX12_STATIC_SAMPLER_DESC samplerDesc(static_cast<UINT>(i),
												filter,
												ConvertAddressMode(sampler.addressU),
												ConvertAddressMode(sampler.addressV),
												ConvertAddressMode(sampler.addressW),
												sampler.mipLODBias,
												sampler.maxAnisotropy,
												D3D12_COMPARISON_FUNC_NEVER,
												D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK,
												sampler.minLOD,
												sampler.maxLOD,
												MapShaderVisibility(sampler.visibility));

		staticSamplers.push_back(samplerDesc);
	}

	// Create root signature
	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
	rootSignatureDesc.Init_1_1(static_cast<UINT>(rootParameters.size()),
							   rootParameters.data(),
							   static_cast<UINT>(staticSamplers.size()),
							   staticSamplers.data(),
							   D3D12_ROOT_SIGNATURE_FLAG_NONE);

	Microsoft::WRL::ComPtr<ID3DBlob> signature;
	Microsoft::WRL::ComPtr<ID3DBlob> error;
	HRESULT hr = D3DX12SerializeVersionedRootSignature(&rootSignatureDesc,
													   D3D_ROOT_SIGNATURE_VERSION_1_1,
													   &signature,
													   &error);

	if (FAILED(hr))
	{
		if (error)
		{
			OutputDebugStringA((char *) error->GetBufferPointer());
		}
		throw D3D12Exception("Failed to serialize compute root signature", hr);
	}

	hr = device_->CreateRootSignature(0,
									  signature->GetBufferPointer(),
									  signature->GetBufferSize(),
									  IID_PPV_ARGS(&rootSignature_));

	if (FAILED(hr))
	{
		throw D3D12Exception("Failed to create compute root signature", hr);
	}

	return *this;
}

D3D12ComputePipeline &D3D12ComputePipeline::BuildRootSignatureFromShader(const ShaderCompilationResult &compileResult)
{
	if (!compileResult.rootSignatureBlob || compileResult.rootSignatureBlob->GetBufferSize() == 0)
	{
		throw D3D12Exception("No root signature found in shader. Make sure to define 'rootSig' in the shader.", E_FAIL);
	}

	HRESULT hr = device_->CreateRootSignature(0,
												  compileResult.rootSignatureBlob->GetBufferPointer(),
												  compileResult.rootSignatureBlob->GetBufferSize(),
												  IID_PPV_ARGS(&rootSignature_));
	if (FAILED(hr))
	{
		throw D3D12Exception("Failed to create root signature from shader", hr);
	}

	return *this;
}

void D3D12ComputePipeline::BuildComputePipeline(const ShaderCompilationResult &compileResult)
{
	if (!rootSignature_)
	{
		throw D3D12Exception("Root signature must be built before compute pipeline", E_FAIL);
	}

	// Find compute shader - try different common entry point names, then fall back to first blob
	auto csIt = compileResult.compiledBlobs.find("computeMain");
	if (csIt == compileResult.compiledBlobs.end())
	{
		csIt = compileResult.compiledBlobs.find("CSMain");
	}
	if (csIt == compileResult.compiledBlobs.end())
	{
		csIt = compileResult.compiledBlobs.find("main");
	}
	if (csIt == compileResult.compiledBlobs.end())
	{
		// Fall back to the first available blob (e.g. Pass0, Pass1, Pass2, etc.)
		csIt = compileResult.compiledBlobs.begin();
	}
	if (csIt == compileResult.compiledBlobs.end())
	{
		throw D3D12Exception("Compute shader is required for compute pipeline", E_INVALIDARG);
	}

	// Create compute pipeline state
	D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = rootSignature_.Get();
	psoDesc.CS = CD3DX12_SHADER_BYTECODE(csIt->second->GetBufferPointer(), csIt->second->GetBufferSize());
	psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

	HRESULT hr = device_->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState_));
	if (FAILED(hr))
	{
		throw D3D12Exception("Failed to create compute pipeline state", hr);
	}
}
