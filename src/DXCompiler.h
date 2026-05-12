#pragma once
#include "pch.h"

#include "D3D12Exception.h"

struct ShaderCompilationResult
{
	std::unordered_map<std::string, Microsoft::WRL::ComPtr<IDxcBlob>> compiledBlobs;
	Microsoft::WRL::ComPtr<IDxcBlob> rootSignatureBlob; // Root signature extracted from shader
	bool success = false;
};

class DXShaderCompiler
{
public:
	DXShaderCompiler()
	{
		HRESULT hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler_));
		if (FAILED(hr))
		{
			throw std::runtime_error("Failed to create DXC compiler instance.");
		}
	}
	~DXShaderCompiler() = default;

	// Compile a single shader entry point
	ShaderCompilationResult CompileShaderFromFile(const std::wstring &shaderPath,
												  const std::wstring &entryPoint,
												  const std::wstring &targetProfile,
												  const std::vector<std::wstring> &arguments = {});

private:
	Microsoft::WRL::ComPtr<IDxcCompiler3> compiler_;
};
