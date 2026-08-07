#pragma once

#include "pch.h"

struct ShaderCompilationResult;

inline constexpr uint32_t kMinRootConstantCount = 1;
inline constexpr uint32_t kMaxRootConstantCount = 16;

inline constexpr uint32_t kStaticSamplerCount = 11;

struct ReflectedRootBinding
{
	D3D12_DESCRIPTOR_RANGE_TYPE type = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	uint32_t registerSpace = 0;
	uint32_t baseShaderRegister = 0;
	uint32_t bindCount = 1;
	uint32_t rootParameterIndex = 0;
	bool isDescriptorTable = false;
	std::string name; // resource variable name, from DXC reflection
};

inline constexpr uint32_t kNoRootConstantParameter = UINT32_MAX;

// A named scalar/vector variable found inside a reflected cbuffer (almost always the implicit
// $Globals DXC packs loose top-level variables into). Lets a single variable be updated by name
// without the caller needing to know which cbuffer DXC happened to pack it into, or at what offset.
struct ReflectedScalar
{
	std::string name; // HLSL variable name, from DXC reflection
	std::string cbufferName; // the cbuffer this variable lives in, e.g. "$Globals"
	uint32_t byteOffset = 0; // offset of this variable within that cbuffer
	uint32_t byteSize = 0;
	uint32_t cbufferByteSize = 0; // total size of the cbuffer named by cbufferName
};

struct ReflectedRootSignature
{
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
	Microsoft::WRL::ComPtr<ID3DBlob> serializedBlob;
	std::vector<ReflectedRootBinding> bindings;
	std::vector<ReflectedScalar> scalars;
	// kNoRootConstantParameter if no g_PushConstants/pushConstants buffer was reflected at all.
	uint32_t rootConstantParameterIndex = kNoRootConstantParameter;
	uint32_t rootConstantCount = 0;
};


ReflectedRootSignature BuildRootSignatureFromReflection(ID3D12Device *device,
														const ShaderCompilationResult &compileResult,
														bool forRaytracing);

Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateRootSignatureForShader(ID3D12Device *device,
																		 const ShaderCompilationResult &compileResult,
																		 bool forRaytracing,
																		 ReflectedRootSignature &outReflected);
