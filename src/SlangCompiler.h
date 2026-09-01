#pragma once
#include "pch.h"

#include "D3D12Exception.h"

#include <span>
#include <string>
#include <vector>

#undef DOMAIN
enum class ShaderStage : uint32_t
{
	VERTEX,
	PIXEL,
	GEOMETRY,
	HULL,
	DOMAIN,
	COMPUTE,
	MESH,
	AMPLIFICATION,
	LIBRARY,
	UNKNOWN
};

const char *ShaderStageToString(ShaderStage stage);

enum class ShaderResourceKind : uint32_t
{
	CBV,
	SRV,
	UAV,
	SAMPLER
};

// One resource the shader binds, as seen by reflection, before it is assigned a root slot.
struct ReflectedResource
{
	std::string name; // resource variable name
	ShaderResourceKind kind = ShaderResourceKind::SRV;
	uint32_t registerSpace = 0;
	uint32_t baseShaderRegister = 0;
	uint32_t bindCount = 1; // 0 == unbounded
	bool isBuffer = false; // structured / byte-address / raw buffer (root-descriptor eligible)
	bool isAccelerationStructure = false;
	bool used = true; // false when the compiled entry point never references this location
};

struct ReflectedConstantBufferVar
{
	std::string name;
	uint32_t byteOffset = 0;
	uint32_t byteSize = 0;
};

// A reflected cbuffer, including the loose top-level globals Slang packs into a default buffer.
struct ReflectedConstantBuffer
{
	std::string name; // e.g. "$Globals", "g_PushConstants"
	uint32_t byteSize = 0;
	std::vector<ReflectedConstantBufferVar> variables;
};

struct ShaderReflectionData
{
	std::vector<ReflectedResource> resources;
	std::vector<ReflectedConstantBuffer> constantBuffers;
	bool usesResourceDescriptorHeapIndexing = false;
	bool usesSamplerDescriptorHeapIndexing = false;
};

struct CompiledShader
{
	std::string entryPoint; // the entry point name ("main" when the caller passed none)
	ShaderStage stage = ShaderStage::UNKNOWN;
	std::vector<uint8_t> bytecode; // DXIL object code
	ShaderReflectionData reflection;
};

struct ShaderCompilationResult
{
	std::vector<CompiledShader> shaders;
	bool success = false;

	const CompiledShader *Find(ShaderStage stage) const;
	bool Has(ShaderStage stage) const
	{
		return Find(stage) != nullptr;
	}

	void Merge(const ShaderCompilationResult &other);
};

struct ShaderEntryPoint
{
	std::wstring entryPoint;
	std::wstring targetProfile;
};

class SlangShaderCompiler
{
public:
	SlangShaderCompiler();
	~SlangShaderCompiler();

	ShaderCompilationResult CompileShaderFromFile(const std::wstring &shaderPath,
												  const std::wstring &entryPoint,
												  const std::wstring &targetProfile,
												  const std::vector<std::wstring> &arguments = {});

	ShaderCompilationResult CompileShaderFromFile(const std::wstring &shaderPath,
												  std::span<const ShaderEntryPoint> entryPoints,
												  const std::vector<std::wstring> &arguments = {});

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
