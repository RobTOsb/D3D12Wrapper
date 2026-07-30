#include "D3D12Pipeline.h"
#include <vector>
#include "D3D12Exception.h"
#include "DXCompiler.h"

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

D3D12GraphicsPipeline &D3D12GraphicsPipeline::BuildRootSignatureFromShader(const ShaderCompilationResult &compileResult)
{
	rootSignature_ = CreateRootSignatureForShader(device_.Get(), compileResult, false, reflectedRootSignature_);
	return *this;
}

void D3D12GraphicsPipeline::BuildGraphicsPipeline(const GraphicsPipelineCreateInfo &pipelineInfo,
												  const ShaderCompilationResult &compileResult)
{
	if (!rootSignature_)
	{
		throw D3D12Exception("Root signature must be built before pipeline", E_FAIL);
	}

	// Mesh shader present? Route to the mesh pipeline path before any VS logic.
	if (compileResult.Has(ShaderStage::MESH))
	{
		BuildMeshShaderPipeline(pipelineInfo, compileResult);
		return;
	}

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = rootSignature_.Get();

	const CompiledShader *vertexShader = compileResult.Find(ShaderStage::VERTEX);
	if (!vertexShader)
	{
		throw D3D12Exception("Vertex shader is required for a graphics pipeline", E_INVALIDARG);
	}

	psoDesc.VS = { vertexShader->blob->GetBufferPointer(), vertexShader->blob->GetBufferSize() };

	// Pixel shader (optional - depth-only passes have none)
	if (const CompiledShader *pixelShader = compileResult.Find(ShaderStage::PIXEL))
	{
		psoDesc.PS = { pixelShader->blob->GetBufferPointer(), pixelShader->blob->GetBufferSize() };
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
	if (const CompiledShader *amplificationShader = compileResult.Find(ShaderStage::AMPLIFICATION))
	{
		psoStream.AS = CD3DX12_SHADER_BYTECODE(amplificationShader->blob->GetBufferPointer(),
											   amplificationShader->blob->GetBufferSize());
	}

	// Mesh shader (required)
	const CompiledShader *meshShader = compileResult.Find(ShaderStage::MESH);
	if (!meshShader)
	{
		throw D3D12Exception("Mesh shader is required for mesh shader pipeline", E_INVALIDARG);
	}
	psoStream.MS = CD3DX12_SHADER_BYTECODE(meshShader->blob->GetBufferPointer(), meshShader->blob->GetBufferSize());

	// Pixel shader (optional but typically present)
	if (const CompiledShader *pixelShader = compileResult.Find(ShaderStage::PIXEL))
	{
		psoStream.PS = CD3DX12_SHADER_BYTECODE(pixelShader->blob->GetBufferPointer(),
											   pixelShader->blob->GetBufferSize());
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

D3D12ComputePipeline &D3D12ComputePipeline::BuildRootSignatureFromShader(const ShaderCompilationResult &compileResult)
{
	rootSignature_ = CreateRootSignatureForShader(device_.Get(), compileResult, false, reflectedRootSignature_);
	return *this;
}

void D3D12ComputePipeline::BuildComputePipeline(const ShaderCompilationResult &compileResult)
{
	if (!rootSignature_)
	{
		throw D3D12Exception("Root signature must be built before compute pipeline", E_FAIL);
	}

	const CompiledShader *computeShader = compileResult.Find(ShaderStage::COMPUTE);
	if (!computeShader)
	{
		throw D3D12Exception("Compute shader is required for compute pipeline", E_INVALIDARG);
	}

	// Create compute pipeline state
	D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = rootSignature_.Get();
	psoDesc.CS = CD3DX12_SHADER_BYTECODE(computeShader->blob->GetBufferPointer(), computeShader->blob->GetBufferSize());
	psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

	HRESULT hr = device_->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState_));
	if (FAILED(hr))
	{
		throw D3D12Exception("Failed to create compute pipeline state", hr);
	}
}

D3D12RaytracingPipeline &D3D12RaytracingPipeline::BuildRootSignatureFromShader(const ShaderCompilationResult &compileResult)
{
	rootSignature_ = CreateRootSignatureForShader(device_.Get(), compileResult, true, reflectedRootSignature_);
	return *this;
}

D3D12RaytracingPipeline &D3D12RaytracingPipeline::SetGlobalRootSignature(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature)
{
	if (!rootSignature)
	{
		throw D3D12Exception("Root signature must not be null", E_INVALIDARG);
	}

	rootSignature_ = rootSignature;
	// A caller-supplied root signature has no reflected layout to report.
	reflectedRootSignature_ = {};
	return *this;
}

void D3D12RaytracingPipeline::BuildRaytracingPipeline(const RaytracingPipelineCreateInfo &pipelineInfo,
													  const ShaderCompilationResult &compileResult)
{
	if (!rootSignature_)
	{
		throw D3D12Exception("Root signature must be built before raytracing pipeline", E_FAIL);
	}

	// Verify device supports DXR
	D3D12_FEATURE_DATA_D3D12_OPTIONS5 features = {};
	HRESULT hr = device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &features, sizeof(features));
	if (FAILED(hr) || features.RaytracingTier == D3D12_RAYTRACING_TIER_NOT_SUPPORTED)
	{
		throw D3D12Exception("Raytracing is not supported on this device", E_NOTIMPL);
	}

	// The whole DXIL library is expected to be compiled as a single lib_6_x blob.
	const CompiledShader *library = compileResult.Find(ShaderStage::LIBRARY);
	if (!library)
	{
		throw D3D12Exception("DXIL library is required for raytracing pipeline", E_INVALIDARG);
	}

	CD3DX12_STATE_OBJECT_DESC raytracingPipelineDesc(D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE);

	// DXIL library + exports
	auto lib = raytracingPipelineDesc.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
	CD3DX12_SHADER_BYTECODE libraryBytecode(library->blob->GetBufferPointer(), library->blob->GetBufferSize());
	lib->SetDXILLibrary(&libraryBytecode);
	for (const auto &exportName: pipelineInfo.exports)
	{
		lib->DefineExport(exportName.c_str());
	}

	// Hit groups
	for (const auto &hitGroupDesc: pipelineInfo.hitGroups)
	{
		auto hitGroup = raytracingPipelineDesc.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
		hitGroup->SetHitGroupType(hitGroupDesc.type);
		hitGroup->SetHitGroupExport(hitGroupDesc.hitGroupExport.c_str());
		if (!hitGroupDesc.closestHitExport.empty())
		{
			hitGroup->SetClosestHitShaderImport(hitGroupDesc.closestHitExport.c_str());
		}
		if (!hitGroupDesc.anyHitExport.empty())
		{
			hitGroup->SetAnyHitShaderImport(hitGroupDesc.anyHitExport.c_str());
		}
		if (!hitGroupDesc.intersectionExport.empty())
		{
			hitGroup->SetIntersectionShaderImport(hitGroupDesc.intersectionExport.c_str());
		}
	}

	// Shader config (payload / attribute sizes)
	auto shaderConfig = raytracingPipelineDesc.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
	shaderConfig->Config(pipelineInfo.maxPayloadSizeInBytes, pipelineInfo.maxAttributeSizeInBytes);

	// Local root signatures + their export associations
	for (const auto &localRootSigAssoc: pipelineInfo.localRootSignatures)
	{
		auto localRootSig = raytracingPipelineDesc.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
		localRootSig->SetRootSignature(localRootSigAssoc.localRootSignature.Get());

		auto association = raytracingPipelineDesc.CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
		association->SetSubobjectToAssociate(*localRootSig);
		for (const auto &exportName: localRootSigAssoc.exports)
		{
			association->AddExport(exportName.c_str());
		}
	}

	// Global root signature
	auto globalRootSig = raytracingPipelineDesc.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
	globalRootSig->SetRootSignature(rootSignature_.Get());

	// Pipeline config (max recursion depth)
	auto pipelineConfig = raytracingPipelineDesc.CreateSubobject<CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT>();
	pipelineConfig->Config(pipelineInfo.maxTraceRecursionDepth);

	Microsoft::WRL::ComPtr<ID3D12Device5> device5;
	hr = device_->QueryInterface(IID_PPV_ARGS(&device5));
	if (FAILED(hr))
	{
		throw D3D12Exception("Device does not support ID3D12Device5", hr);
	}

	hr = device5->CreateStateObject(raytracingPipelineDesc, IID_PPV_ARGS(&stateObject_));
	if (FAILED(hr))
	{
		throw D3D12Exception("Failed to create raytracing pipeline state object", hr);
	}

	hr = stateObject_->QueryInterface(IID_PPV_ARGS(&stateObjectProperties_));
	if (FAILED(hr))
	{
		throw D3D12Exception("Failed to query ID3D12StateObjectProperties from raytracing state object", hr);
	}
}
