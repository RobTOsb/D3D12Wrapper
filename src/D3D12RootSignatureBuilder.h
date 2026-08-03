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

struct ReflectedRootSignature
{
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
	Microsoft::WRL::ComPtr<ID3DBlob> serializedBlob; 
	std::vector<ReflectedRootBinding> bindings;
	uint32_t rootConstantParameterIndex = 0;
	uint32_t rootConstantCount = 0;
};


ReflectedRootSignature BuildRootSignatureFromReflection(ID3D12Device *device,
														const ShaderCompilationResult &compileResult,
														bool forRaytracing);

Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateRootSignatureForShader(ID3D12Device *device,
																		 const ShaderCompilationResult &compileResult,
																		 bool forRaytracing,
																		 ReflectedRootSignature &outReflected);
