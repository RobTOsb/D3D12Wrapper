#include "DXCompiler.h"
#include <fstream>
#include <vector>

#include "fmtlog.h"

ShaderCompilationResult DXShaderCompiler::CompileShaderFromFile(const std::wstring &shaderPath,
																const std::wstring &entryPoint,
																const std::wstring &targetProfile,
																const std::vector<std::wstring> &arguments)
{
	ShaderCompilationResult result;
	// Read the shader file
	std::ifstream shaderFile(shaderPath, std::ios::binary | std::ios::ate);
	if (!shaderFile.is_open())
	{
		std::string narrowPath(shaderPath.begin(), shaderPath.end());
		logw("Failed to open shader file: {}", narrowPath.c_str());
		return result;
	}

	std::streamsize fileSize = shaderFile.tellg();
	shaderFile.seekg(0, std::ios::beg);

	std::vector<char> shaderCode(fileSize);
	if (!shaderFile.read(shaderCode.data(), fileSize))
	{
		std::string narrowPath(shaderPath.begin(), shaderPath.end());
		logw("Failed to read shader file: {}", narrowPath.c_str());
		return result;
	}
	shaderFile.close();

	// Create DXC utils for file operations
	Microsoft::WRL::ComPtr<IDxcUtils> utils;
	HRESULT hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils));
	if (FAILED(hr))
	{
		logw("Failed to create DXC utils instance");
		return result;
	}

	// Create include handler
	Microsoft::WRL::ComPtr<IDxcIncludeHandler> includeHandler;
	hr = utils->CreateDefaultIncludeHandler(&includeHandler);
	if (FAILED(hr))
	{
		logw("Failed to create include handler");
		return result;
	}

	// Create a blob from the shader code
	Microsoft::WRL::ComPtr<IDxcBlobEncoding> sourceBlob;
	hr = utils->CreateBlob(shaderCode.data(), static_cast<UINT32>(shaderCode.size()), CP_UTF8, &sourceBlob);
	if (FAILED(hr))
	{
		logw("Failed to create source blob");
		return result;
	}

	// Build compilation arguments
	std::vector<LPCWSTR> compileArgs;

	// Extract shader directory for include paths
	std::wstring shaderDir = shaderPath.substr(0, shaderPath.find_last_of(L"\\/"));

	// Add include directory (shader file's directory)
	compileArgs.push_back(L"-I");
	compileArgs.push_back(shaderDir.c_str());

	// Entry point
	if (!entryPoint.empty())
	{
		compileArgs.push_back(L"-E");
		compileArgs.push_back(entryPoint.c_str());
	}

	// Target profile
	if (!targetProfile.empty())
	{
		compileArgs.push_back(L"-T");
		compileArgs.push_back(targetProfile.c_str());
	}

	// Add shader model 6.6
	compileArgs.push_back(L"-HV");
	compileArgs.push_back(L"2021");

	// Enable 16-bit types
	compileArgs.push_back(L"-enable-16bit-types");
	
	// Debug info in debug builds
#ifdef _DEBUG
	compileArgs.push_back(DXC_ARG_DEBUG);
	compileArgs.push_back(DXC_ARG_SKIP_OPTIMIZATIONS);
#else
	compileArgs.push_back(DXC_ARG_OPTIMIZATION_LEVEL3);
#endif

	// Embed root signature from the shader
	compileArgs.push_back(L"-rootsig-define");
	compileArgs.push_back(L"rootSig");
	// Enable debug info
	compileArgs.push_back(L"-Zi");
	compileArgs.push_back(L"-Qembed_debug");
	// Additional user-specified arguments
	for (const auto &arg: arguments)
	{
		compileArgs.push_back(arg.c_str());
	}

	// Prepare source buffer
	DxcBuffer sourceBuffer;
	sourceBuffer.Ptr = sourceBlob->GetBufferPointer();
	sourceBuffer.Size = sourceBlob->GetBufferSize();
	sourceBuffer.Encoding = CP_UTF8;

	// Compile the shader
	Microsoft::WRL::ComPtr<IDxcResult> compileResult;
	hr = compiler_->Compile(&sourceBuffer,
							compileArgs.data(),
							static_cast<UINT32>(compileArgs.size()),
							includeHandler.Get(),
							IID_PPV_ARGS(&compileResult));

	if (FAILED(hr))
	{
		logw("Failed to compile shader");
		return result;
	}

	// Check compilation status
	HRESULT compileStatus;
	compileResult->GetStatus(&compileStatus);

	// Get error/warning messages
	Microsoft::WRL::ComPtr<IDxcBlobUtf8> errors;
	compileResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
	if (errors && errors->GetStringLength() > 0)
	{
		logw("Shader compilation messages:\n{}", errors->GetStringPointer());
		fmtlog::poll();
	}

	if (FAILED(compileStatus))
	{
		logw("Shader compilation failed");
		return result;
	}

	// Get the compiled shader blob
	Microsoft::WRL::ComPtr<IDxcBlob> shaderBlob;
	hr = compileResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
	if (FAILED(hr) || !shaderBlob)
	{
		logw("Failed to get compiled shader blob");
		return result;
	}

	// Store the compiled blob for the entry point
	std::string entryPointName = entryPoint.empty() ? "main" : std::string(entryPoint.begin(), entryPoint.end());
	result.compiledBlobs[entryPointName] = shaderBlob;

	// Try to get the root signature from the shader
	Microsoft::WRL::ComPtr<IDxcBlob> rootSigBlob;
	hr = compileResult->GetOutput(DXC_OUT_ROOT_SIGNATURE, IID_PPV_ARGS(&rootSigBlob), nullptr);
	if (SUCCEEDED(hr) && rootSigBlob && rootSigBlob->GetBufferSize() > 0)
	{
		result.rootSignatureBlob = rootSigBlob;
	}

	result.success = true;
	return result;
}
