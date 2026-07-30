#include "DXCompiler.h"
#include <fstream>
#include <vector>

#include "fmtlog.h"

const char *ShaderStageToString(ShaderStage stage)
{
	switch (stage)
	{
		case ShaderStage::VERTEX:
			return "vertex";
		case ShaderStage::PIXEL:
			return "pixel";
		case ShaderStage::GEOMETRY:
			return "geometry";
		case ShaderStage::HULL:
			return "hull";
		case ShaderStage::DOMAIN:
			return "domain";
		case ShaderStage::COMPUTE:
			return "compute";
		case ShaderStage::MESH:
			return "mesh";
		case ShaderStage::AMPLIFICATION:
			return "amplification";
		case ShaderStage::LIBRARY:
			return "library";
		default:
			return "unknown";
	}
}

const CompiledShader *ShaderCompilationResult::Find(ShaderStage stage) const
{
	for (const auto &shader: shaders)
	{
		if (shader.stage == stage)
		{
			return &shader;
		}
	}
	return nullptr;
}

void ShaderCompilationResult::Merge(const ShaderCompilationResult &other)
{
	shaders.insert(shaders.end(), other.shaders.begin(), other.shaders.end());
	if (!rootSignatureBlob && other.rootSignatureBlob)
	{
		rootSignatureBlob = other.rootSignatureBlob;
	}
	success = success && other.success;
}

static ShaderStage StageFromTargetProfile(const std::wstring &targetProfile)
{
	const std::wstring prefix = targetProfile.substr(0, targetProfile.find(L'_'));

	if (prefix == L"vs")
		return ShaderStage::VERTEX;
	if (prefix == L"ps")
		return ShaderStage::PIXEL;
	if (prefix == L"gs")
		return ShaderStage::GEOMETRY;
	if (prefix == L"hs")
		return ShaderStage::HULL;
	if (prefix == L"ds")
		return ShaderStage::DOMAIN;
	if (prefix == L"cs")
		return ShaderStage::COMPUTE;
	if (prefix == L"ms")
		return ShaderStage::MESH;
	if (prefix == L"as")
		return ShaderStage::AMPLIFICATION;
	if (prefix == L"lib")
		return ShaderStage::LIBRARY;

	return ShaderStage::UNKNOWN;
}

DXShaderCompiler::DXShaderCompiler()
{
	HRESULT hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler_));
	if (FAILED(hr))
	{
		throw std::runtime_error("Failed to create DXC compiler instance.");
	}

	hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils_));
	if (FAILED(hr))
	{
		throw std::runtime_error("Failed to create DXC utils instance.");
	}

	hr = utils_->CreateDefaultIncludeHandler(&includeHandler_);
	if (FAILED(hr))
	{
		throw std::runtime_error("Failed to create DXC include handler.");
	}
}

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

	// Create a blob from the shader code
	Microsoft::WRL::ComPtr<IDxcBlobEncoding> sourceBlob;
	HRESULT hr = utils_->CreateBlob(shaderCode.data(), static_cast<UINT32>(shaderCode.size()), CP_UTF8, &sourceBlob);
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
	for (const auto &arg: arguments)
	{
		if (!arg.empty() && (arg.front() == L'-' || arg.front() == L'/'))
		{
			compileArgs.push_back(arg.c_str());
		}
		else
		{
			compileArgs.push_back(L"-D");
			compileArgs.push_back(arg.c_str());
		}
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
							includeHandler_.Get(),
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

	CompiledShader compiledShader;
	compiledShader.entryPoint = entryPoint.empty() ? "main" : std::string(entryPoint.begin(), entryPoint.end());
	compiledShader.stage = StageFromTargetProfile(targetProfile);
	compiledShader.blob = shaderBlob;

	Microsoft::WRL::ComPtr<IDxcBlob> reflectionBlob;
	compileResult->GetOutput(DXC_OUT_REFLECTION, IID_PPV_ARGS(&reflectionBlob), nullptr);
	if (reflectionBlob && reflectionBlob->GetBufferSize() > 0)
	{
		compiledShader.reflectionBlob = reflectionBlob;
	}

	IDxcBlob *reflectionSource = compiledShader.reflectionBlob ? compiledShader.reflectionBlob.Get() : shaderBlob.Get();
	DxcBuffer reflectionBuffer = { reflectionSource->GetBufferPointer(), reflectionSource->GetBufferSize(), 0 };

	if (compiledShader.stage == ShaderStage::LIBRARY)
	{
		hr = utils_->CreateReflection(&reflectionBuffer, IID_PPV_ARGS(&compiledShader.libraryReflection));
	}
	else
	{
		hr = utils_->CreateReflection(&reflectionBuffer, IID_PPV_ARGS(&compiledShader.shaderReflection));
	}
	if (FAILED(hr))
	{
		logw("Failed to create shader reflection for entry point '{}' ({} shader)",
			 compiledShader.entryPoint.c_str(),
			 ShaderStageToString(compiledShader.stage));
	}

	result.shaders.push_back(std::move(compiledShader));

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

ShaderCompilationResult DXShaderCompiler::CompileShaderFromFile(const std::wstring &shaderPath,
																std::span<const ShaderEntryPoint> entryPoints,
																const std::vector<std::wstring> &arguments)
{
	ShaderCompilationResult result;
	if (entryPoints.empty())
	{
		logw("CompileShaderFromFile called with no entry points");
		return result;
	}

	result.success = true;
	for (const auto &entry: entryPoints)
	{
		result.Merge(CompileShaderFromFile(shaderPath, entry.entryPoint, entry.targetProfile, arguments));
		if (!result.success)
		{
			return result;
		}
	}

	return result;
}
