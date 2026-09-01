#include "D3D12RootSignatureBuilder.h"

#include <algorithm>

#include "D3D12Exception.h"
#include "SlangCompiler.h"
#include "fmtlog.h"

namespace
{
	struct ReflectedBinding
	{
		D3D12_DESCRIPTOR_RANGE_TYPE rangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		bool isBuffer = false; // structured / byte-address / raw buffer
		bool isAccelStruct = false;
		uint32_t registerSpace = 0;
		uint32_t baseShaderRegister = 0;
		uint32_t bindCount = 1; // 0 means unbounded
		std::string name;
	};

	const char *DescriptorRangeTypeToString(D3D12_DESCRIPTOR_RANGE_TYPE type)
	{
		switch (type)
		{
			case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
				return "SRV";
			case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
				return "UAV";
			case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
				return "CBV";
			default:
				return "SAMPLER";
		}
	}

	D3D12_DESCRIPTOR_RANGE_TYPE RangeTypeFromResourceKind(ShaderResourceKind kind)
	{
		switch (kind)
		{
			case ShaderResourceKind::CBV:
				return D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
			case ShaderResourceKind::UAV:
				return D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
			case ShaderResourceKind::SAMPLER:
				return D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
			default:
				return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		}
	}

	bool CanBeRootDescriptor(const ReflectedBinding &binding)
	{
		if (binding.bindCount != 1)
		{
			return false;
		}

		if (binding.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_CBV)
		{
			return true;
		}

		if (binding.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER)
		{
			return false;
		}
		return binding.isBuffer || binding.isAccelStruct;
	}

	void AddBinding(std::vector<ReflectedBinding> &bindings, const ReflectedResource &resource)
	{
		// A resource whose register the compiler could not resolve (no explicit register() on
		// a library export, for instance) cannot be placed in a root signature.
		if (resource.baseShaderRegister == UINT_MAX || resource.registerSpace == UINT_MAX)
		{
			logw("Resource '{}' has no assigned register and was left out of the generated root "
				 "signature; declare it with an explicit register() binding",
				 resource.name.empty() ? "<unnamed>" : resource.name.c_str());
			return;
		}

		const D3D12_DESCRIPTOR_RANGE_TYPE rangeType = RangeTypeFromResourceKind(resource.kind);

		// The same resource shows up once per stage that uses it; keep the widest binding.
		for (auto &existing: bindings)
		{
			if (existing.rangeType == rangeType && existing.registerSpace == resource.registerSpace &&
				existing.baseShaderRegister == resource.baseShaderRegister)
			{
				if (existing.bindCount != 0 && (resource.bindCount == 0 || resource.bindCount > existing.bindCount))
				{
					existing.bindCount = resource.bindCount;
				}
				return;
			}
		}

		ReflectedBinding binding;
		binding.rangeType = rangeType;
		binding.isBuffer = resource.isBuffer;
		binding.isAccelStruct = resource.isAccelerationStructure;
		binding.registerSpace = resource.registerSpace;
		binding.baseShaderRegister = resource.baseShaderRegister;
		binding.bindCount = resource.bindCount;
		binding.name = resource.name;
		bindings.push_back(std::move(binding));
	}

	// Size in bytes of the named constant buffer, or 0 if reflection does not know it.
	uint32_t ConstantBufferSize(const CompiledShader &shader, const char *name)
	{
		if (!name)
		{
			return 0;
		}

		for (const auto &cbuffer: shader.reflection.constantBuffers)
		{
			if (cbuffer.name == name)
			{
				return cbuffer.byteSize;
			}
		}
		return 0;
	}

	void AddScalarsFromConstantBuffer(const ReflectedConstantBuffer &cbuffer, std::vector<ReflectedScalar> &outScalars)
	{
		for (const auto &variable: cbuffer.variables)
		{
			const bool alreadyKnown =
					std::any_of(outScalars.begin(),
								outScalars.end(),
								[&](const ReflectedScalar &scalar) { return scalar.name == variable.name; });
			if (alreadyKnown)
			{
				continue;
			}

			ReflectedScalar scalar;
			scalar.name = variable.name;
			scalar.cbufferName = cbuffer.name;
			scalar.byteOffset = variable.byteOffset;
			scalar.byteSize = variable.byteSize;
			scalar.cbufferByteSize = cbuffer.byteSize;
			outScalars.push_back(std::move(scalar));
		}
	}

	// Walks every compiled stage and collects every variable in every reflected cbuffer, by name.
	void CollectScalars(const ShaderCompilationResult &compileResult, std::vector<ReflectedScalar> &outScalars)
	{
		for (const auto &shader: compileResult.shaders)
		{
			for (const auto &cbuffer: shader.reflection.constantBuffers)
			{
				AddScalarsFromConstantBuffer(cbuffer, outScalars);
			}
		}
	}

	// Walks every compiled stage and unions the resources they bind.
	void CollectBindings(const ShaderCompilationResult &compileResult,
						 std::vector<ReflectedBinding> &outBindings,
						 bool &outUsesResourceHeapIndexing,
						 bool &outUsesSamplerHeapIndexing)
	{
		outUsesResourceHeapIndexing = false;
		outUsesSamplerHeapIndexing = false;

		for (const auto &shader: compileResult.shaders)
		{
			outUsesResourceHeapIndexing |= shader.reflection.usesResourceDescriptorHeapIndexing;
			outUsesSamplerHeapIndexing |= shader.reflection.usesSamplerDescriptorHeapIndexing;

			for (const auto &resource: shader.reflection.resources)
			{
				// Slang reports which locations the compiled entry point actually references;
				// unused ones are dropped exactly as DXC pruned them implicitly.
				if (!resource.used)
				{
					continue;
				}
				AddBinding(outBindings, resource);
			}
		}
	}

	D3D12_STATIC_SAMPLER_DESC MakeStaticSampler(
			uint32_t shaderRegister,
			D3D12_FILTER filter,
			D3D12_TEXTURE_ADDRESS_MODE addressMode,
			D3D12_STATIC_BORDER_COLOR borderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE,
			D3D12_COMPARISON_FUNC comparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL)
	{
		D3D12_STATIC_SAMPLER_DESC sampler = {};
		sampler.Filter = filter;
		sampler.AddressU = addressMode;
		sampler.AddressV = addressMode;
		sampler.AddressW = addressMode;
		sampler.MipLODBias = 0.0f;
		sampler.MaxAnisotropy = 16;
		sampler.ComparisonFunc = comparisonFunc;
		sampler.BorderColor = borderColor;
		sampler.MinLOD = 0.0f;
		sampler.MaxLOD = D3D12_FLOAT32_MAX;
		sampler.ShaderRegister = shaderRegister;
		sampler.RegisterSpace = 0;
		sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		return sampler;
	}

	std::array<D3D12_STATIC_SAMPLER_DESC, kStaticSamplerCount> StandardStaticSamplers()
	{
		return {
			MakeStaticSampler(0, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP),
			MakeStaticSampler(1, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_WRAP),
			MakeStaticSampler(2, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP),
			MakeStaticSampler(3, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP),
			MakeStaticSampler(4, D3D12_FILTER_ANISOTROPIC, D3D12_TEXTURE_ADDRESS_MODE_CLAMP),
			MakeStaticSampler(5, D3D12_FILTER_ANISOTROPIC, D3D12_TEXTURE_ADDRESS_MODE_WRAP),
			MakeStaticSampler(6,
							  D3D12_FILTER_MIN_MAG_MIP_LINEAR,
							  D3D12_TEXTURE_ADDRESS_MODE_BORDER,
							  D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK),
			MakeStaticSampler(7,
							  D3D12_FILTER_MIN_MAG_MIP_LINEAR,
							  D3D12_TEXTURE_ADDRESS_MODE_BORDER,
							  D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE),
			MakeStaticSampler(8,
							  D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT,
							  D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
							  D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE,
							  D3D12_COMPARISON_FUNC_LESS_EQUAL),
			MakeStaticSampler(9, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_MIRROR),
			MakeStaticSampler(10, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP),
		};
	}

	D3D_ROOT_SIGNATURE_VERSION HighestRootSignatureVersion(ID3D12Device *device)
	{
		D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
		featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
		if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
		{
			return D3D_ROOT_SIGNATURE_VERSION_1_0;
		}
		return featureData.HighestVersion;
	}

	// Root constants cost one DWORD each, root descriptors two, descriptor tables one.
	uint32_t RootSignatureCostInDwords(const std::vector<D3D12_ROOT_PARAMETER1> &parameters)
	{
		uint32_t cost = 0;
		for (const auto &parameter: parameters)
		{
			switch (parameter.ParameterType)
			{
				case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
					cost += parameter.Constants.Num32BitValues;
					break;
				case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
					cost += 1;
					break;
				default:
					cost += 2;
					break;
			}
		}
		return cost;
	}
} // namespace

ReflectedRootSignature BuildRootSignatureFromReflection(ID3D12Device *device,
														const ShaderCompilationResult &compileResult,
														bool forRaytracing)
{
	if (!device)
	{
		throw D3D12Exception("Cannot build a root signature without a device", E_INVALIDARG);
	}
	if (compileResult.shaders.empty())
	{
		throw D3D12Exception("Cannot build a root signature from an empty compilation result", E_INVALIDARG);
	}

	std::vector<ReflectedBinding> bindings;
	bool usesResourceHeapIndexing = false;
	bool usesSamplerHeapIndexing = false;
	CollectBindings(compileResult, bindings, usesResourceHeapIndexing, usesSamplerHeapIndexing);
	logd("Reflected root signature: {} binding(s), bindless resource heap: {}, bindless sampler heap: {}",
		 bindings.size(),
		 usesResourceHeapIndexing,
		 usesSamplerHeapIndexing);
	for (const auto &binding: bindings)
	{
		logd("  {} '{}' register {}, space {}, count {}",
			 DescriptorRangeTypeToString(binding.rangeType),
			 binding.name.c_str(),
			 binding.baseShaderRegister,
			 binding.registerSpace,
			 binding.bindCount);
	}

	ReflectedRootSignature result;
	CollectScalars(compileResult, result.scalars);

	bool hasRootConstants = false;
	uint32_t rootConstantCount = 0;
	uint32_t rootConstantsRegister = 0;
	uint32_t rootConstantsSpace = 0;
	for (auto it = bindings.begin(); it != bindings.end(); ++it)
	{
		if (it->rangeType != D3D12_DESCRIPTOR_RANGE_TYPE_CBV ||
			(it->name != "g_PushConstants" && it->name != "pushConstants"))
		{
			continue;
		}

		uint32_t sizeInBytes = 0;
		for (const auto &shader: compileResult.shaders)
		{
			sizeInBytes = ConstantBufferSize(shader, it->name.c_str());
			if (sizeInBytes > 0)
			{
				break;
			}
		}

		const uint32_t requiredDwords = (sizeInBytes + 3) / 4;
		rootConstantCount = (std::min) ((std::max) (kMinRootConstantCount, requiredDwords), kMaxRootConstantCount);
		hasRootConstants = true;
		rootConstantsRegister = it->baseShaderRegister;
		rootConstantsSpace = it->registerSpace;

		// The root-constants parameter replaces this binding rather than being additional to it.
		bindings.erase(it);
		break;
	}

	std::sort(bindings.begin(),
			  bindings.end(),
			  [](const ReflectedBinding &a, const ReflectedBinding &b)
			  {
				  // Purely for deterministic root parameter ordering; unbounded ranges sort last.
				  const bool aUnbounded = a.bindCount == 0;
				  const bool bUnbounded = b.bindCount == 0;
				  if (aUnbounded != bUnbounded)
					  return bUnbounded;
				  if (a.rangeType != b.rangeType)
					  return a.rangeType < b.rangeType;
				  if (a.registerSpace != b.registerSpace)
					  return a.registerSpace < b.registerSpace;
				  return a.baseShaderRegister < b.baseShaderRegister;
			  });

	std::vector<D3D12_ROOT_PARAMETER1> parameters;

	if (hasRootConstants)
	{
		D3D12_ROOT_PARAMETER1 rootConstants = {};
		rootConstants.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
		rootConstants.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		rootConstants.Constants.ShaderRegister = rootConstantsRegister;
		rootConstants.Constants.RegisterSpace = rootConstantsSpace;
		rootConstants.Constants.Num32BitValues = rootConstantCount;

		result.rootConstantParameterIndex = static_cast<uint32_t>(parameters.size());
		result.rootConstantCount = rootConstantCount;
		parameters.push_back(rootConstants);
	}

	std::vector<std::vector<D3D12_DESCRIPTOR_RANGE1>> tables; // one range each
	std::vector<size_t> tableBindingIndices;

	for (const auto &binding: bindings)
	{
		if (binding.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER)
		{
			// Samplers are served by the static sampler set below.
			if (binding.registerSpace != 0 || binding.baseShaderRegister >= kStaticSamplerCount)
			{
				logw("Shader binds sampler '{}' at s{}, space{}, which is outside the static sampler "
					 "set (s0..s{}, space0) baked into generated root signatures",
					 binding.name.c_str(),
					 binding.baseShaderRegister,
					 binding.registerSpace,
					 kStaticSamplerCount - 1);
			}
			continue;
		}

		ReflectedRootBinding record;
		record.type = binding.rangeType;
		record.registerSpace = binding.registerSpace;
		record.baseShaderRegister = binding.baseShaderRegister;
		record.bindCount = binding.bindCount;
		record.name = binding.name;

		if (CanBeRootDescriptor(binding))
		{
			D3D12_ROOT_PARAMETER1 parameter = {};
			switch (binding.rangeType)
			{
				case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
					parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
					break;
				case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
					parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
					break;
				default:
					parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
					break;
			}
			parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
			parameter.Descriptor.ShaderRegister = binding.baseShaderRegister;
			parameter.Descriptor.RegisterSpace = binding.registerSpace;
			// Nothing here knows how the caller will use the resource, so promise nothing.
			parameter.Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE;

			record.isDescriptorTable = false;
			record.rootParameterIndex = static_cast<uint32_t>(parameters.size());
			parameters.push_back(parameter);
			result.bindings.push_back(record);
		}
		else
		{
			D3D12_DESCRIPTOR_RANGE1 range = {};
			range.RangeType = binding.rangeType;
			range.NumDescriptors = binding.bindCount == 0 ? UINT_MAX : binding.bindCount;
			range.BaseShaderRegister = binding.baseShaderRegister;
			range.RegisterSpace = binding.registerSpace;
			range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE | D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;
			// Sole range in its table, so it starts at the base handle the binder sets.
			range.OffsetInDescriptorsFromTableStart = 0;

			record.isDescriptorTable = true;

			tables.push_back({ range });
			tableBindingIndices.push_back(result.bindings.size());
			result.bindings.push_back(record);
		}
	}

	for (size_t i = 0; i < tables.size(); ++i)
	{
		const uint32_t tableParameterIndex = static_cast<uint32_t>(parameters.size());

		D3D12_ROOT_PARAMETER1 parameter = {};
		parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		parameter.DescriptorTable.NumDescriptorRanges = 1;
		parameter.DescriptorTable.pDescriptorRanges = tables[i].data();
		parameters.push_back(parameter);

		result.bindings[tableBindingIndices[i]].rootParameterIndex = tableParameterIndex;
	}

	const uint32_t costInDwords = RootSignatureCostInDwords(parameters);
	if (costInDwords > D3D12_MAX_ROOT_COST)
	{
		logw("Reflected root signature needs {} DWORDs, over the {} DWORD limit", costInDwords, D3D12_MAX_ROOT_COST);
		throw D3D12Exception("Reflected root signature exceeds the 64 DWORD root cost limit", E_INVALIDARG);
	}

	D3D12_ROOT_SIGNATURE_FLAGS flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
									   D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED;
	if (!forRaytracing && compileResult.Has(ShaderStage::VERTEX))
	{
		flags |= D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	}

	const auto staticSamplers = StandardStaticSamplers();

	D3D12_VERSIONED_ROOT_SIGNATURE_DESC versionedDesc = {};
	versionedDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
	versionedDesc.Desc_1_1.NumParameters = static_cast<UINT>(parameters.size());
	versionedDesc.Desc_1_1.pParameters = parameters.data();
	versionedDesc.Desc_1_1.NumStaticSamplers = static_cast<UINT>(staticSamplers.size());
	versionedDesc.Desc_1_1.pStaticSamplers = staticSamplers.data();
	versionedDesc.Desc_1_1.Flags = flags;

	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
	HRESULT hr = D3DX12SerializeVersionedRootSignature(&versionedDesc,
													   HighestRootSignatureVersion(device),
													   &result.serializedBlob,
													   &errorBlob);
	if (FAILED(hr))
	{
		if (errorBlob && errorBlob->GetBufferSize() > 0)
		{
			logw("Failed to serialize reflected root signature: {}",
				 static_cast<const char *>(errorBlob->GetBufferPointer()));
			fmtlog::poll();
		}
		throw D3D12Exception("Failed to serialize root signature built from shader reflection", hr);
	}

	hr = device->CreateRootSignature(0,
									 result.serializedBlob->GetBufferPointer(),
									 result.serializedBlob->GetBufferSize(),
									 IID_PPV_ARGS(&result.rootSignature));
	if (FAILED(hr))
	{
		throw D3D12Exception("Failed to create root signature built from shader reflection", hr);
	}

	return result;
}

Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateRootSignatureForShader(ID3D12Device *device,
																		 const ShaderCompilationResult &compileResult,
																		 bool forRaytracing,
																		 ReflectedRootSignature &outReflected)
{
	outReflected = {};

	outReflected = BuildRootSignatureFromReflection(device, compileResult, forRaytracing);
	return outReflected.rootSignature;
}
