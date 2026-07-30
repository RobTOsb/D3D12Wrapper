#pragma once
#include "pch.h"

#include "D3D12Exception.h"

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

struct CompiledShader
{
	std::string entryPoint; // as given to -E ("main" when the caller passed none)
	ShaderStage stage = ShaderStage::UNKNOWN;
	Microsoft::WRL::ComPtr<IDxcBlob> blob; // DXC_OUT_OBJECT
	Microsoft::WRL::ComPtr<IDxcBlob> reflectionBlob; // DXC_OUT_REFLECTION, retained for caching
	Microsoft::WRL::ComPtr<ID3D12ShaderReflection> shaderReflection; // non-library targets
	Microsoft::WRL::ComPtr<ID3D12LibraryReflection> libraryReflection; // lib_6_x targets
};

struct ShaderCompilationResult
{
	std::vector<CompiledShader> shaders;
	Microsoft::WRL::ComPtr<IDxcBlob> rootSignatureBlob; // Root signature extracted from shader
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

class DXShaderCompiler
{
public:
	DXShaderCompiler();
	~DXShaderCompiler() = default;

	ShaderCompilationResult CompileShaderFromFile(const std::wstring &shaderPath,
												  const std::wstring &entryPoint,
												  const std::wstring &targetProfile,
												  const std::vector<std::wstring> &arguments = {});

	ShaderCompilationResult CompileShaderFromFile(const std::wstring &shaderPath,
												  std::span<const ShaderEntryPoint> entryPoints,
												  const std::vector<std::wstring> &arguments = {});

private:
	Microsoft::WRL::ComPtr<IDxcCompiler3> compiler_;
	Microsoft::WRL::ComPtr<IDxcUtils> utils_;
	Microsoft::WRL::ComPtr<IDxcIncludeHandler> includeHandler_;
};
