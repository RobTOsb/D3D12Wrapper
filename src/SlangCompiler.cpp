#include "SlangCompiler.h"

#include <cstdint>
#include <deque>
#include <fstream>
#include <string>
#include <vector>

#include <slang-com-ptr.h>
#include <slang.h>

#include "fmtlog.h"

using Slang::ComPtr;

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
	success = success && other.success;
}

namespace
{
	std::string Narrow(const std::wstring &wide)
	{
		if (wide.empty())
		{
			return {};
		}
		const int size = WideCharToMultiByte(CP_UTF8,
											 0,
											 wide.data(),
											 static_cast<int>(wide.size()),
											 nullptr,
											 0,
											 nullptr,
											 nullptr);
		std::string result(static_cast<size_t>(size), '\0');
		WideCharToMultiByte(CP_UTF8,
							0,
							wide.data(),
							static_cast<int>(wide.size()),
							result.data(),
							size,
							nullptr,
							nullptr);
		return result;
	}

	struct ParsedTargetProfile
	{
		ShaderStage stage = ShaderStage::UNKNOWN;
		SlangStage slangStage = SLANG_STAGE_NONE;
		std::string profile;
		bool isLibrary = false;
	};

	ShaderStage StageFromPrefix(const std::string &prefix)
	{
		if (prefix == "vs")
			return ShaderStage::VERTEX;
		if (prefix == "ps")
			return ShaderStage::PIXEL;
		if (prefix == "gs")
			return ShaderStage::GEOMETRY;
		if (prefix == "hs")
			return ShaderStage::HULL;
		if (prefix == "ds")
			return ShaderStage::DOMAIN;
		if (prefix == "cs")
			return ShaderStage::COMPUTE;
		if (prefix == "ms")
			return ShaderStage::MESH;
		if (prefix == "as")
			return ShaderStage::AMPLIFICATION;
		if (prefix == "lib")
			return ShaderStage::LIBRARY;
		return ShaderStage::UNKNOWN;
	}

	SlangStage ToSlangStage(ShaderStage stage)
	{
		switch (stage)
		{
			case ShaderStage::VERTEX:
				return SLANG_STAGE_VERTEX;
			case ShaderStage::PIXEL:
				return SLANG_STAGE_FRAGMENT;
			case ShaderStage::GEOMETRY:
				return SLANG_STAGE_GEOMETRY;
			case ShaderStage::HULL:
				return SLANG_STAGE_HULL;
			case ShaderStage::DOMAIN:
				return SLANG_STAGE_DOMAIN;
			case ShaderStage::COMPUTE:
				return SLANG_STAGE_COMPUTE;
			case ShaderStage::MESH:
				return SLANG_STAGE_MESH;
			case ShaderStage::AMPLIFICATION:
				return SLANG_STAGE_AMPLIFICATION;
			default:
				return SLANG_STAGE_NONE;
		}
	}

	ParsedTargetProfile ParseTargetProfile(const std::wstring &targetProfile)
	{
		ParsedTargetProfile parsed;
		const std::string profile = Narrow(targetProfile);

		const size_t underscore = profile.find('_');
		const std::string prefix = underscore == std::string::npos ? profile : profile.substr(0, underscore);

		parsed.stage = StageFromPrefix(prefix);
		parsed.isLibrary = parsed.stage == ShaderStage::LIBRARY;
		parsed.slangStage = ToSlangStage(parsed.stage);

		if (underscore != std::string::npos && underscore + 1 < profile.size())
		{
			parsed.profile = "sm_" + profile.substr(underscore + 1);
		}
		else
		{
			parsed.profile = "sm_6_6";
		}

		return parsed;
	}

	void LogDiagnostics(slang::IBlob *diagnostics)
	{
		if (diagnostics && diagnostics->getBufferSize() > 0)
		{
			logw("Shader compilation messages:\n{}", static_cast<const char *>(diagnostics->getBufferPointer()));
			fmtlog::poll();
		}
	}

	SlangParameterCategory CategoryForKind(ShaderResourceKind kind)
	{
		switch (kind)
		{
			case ShaderResourceKind::CBV:
				return SLANG_PARAMETER_CATEGORY_CONSTANT_BUFFER;
			case ShaderResourceKind::UAV:
				return SLANG_PARAMETER_CATEGORY_UNORDERED_ACCESS;
			case ShaderResourceKind::SAMPLER:
				return SLANG_PARAMETER_CATEGORY_SAMPLER_STATE;
			default:
				return SLANG_PARAMETER_CATEGORY_SHADER_RESOURCE;
		}
	}

	uint32_t NormalizeRegister(size_t value)
	{
		if (value == SLANG_UNBOUNDED_SIZE || value == SLANG_UNKNOWN_SIZE || value > 0xFFFFFFF0ull)
		{
			return UINT32_MAX;
		}
		return static_cast<uint32_t>(value);
	}

	uint32_t BindCountFromTypeLayout(slang::TypeLayoutReflection *typeLayout)
	{
		if (!typeLayout || !typeLayout->getType())
		{
			return 1;
		}
		if (typeLayout->getKind() != slang::TypeReflection::Kind::Array)
		{
			return 1;
		}
		const size_t count = typeLayout->getType()->getElementCount();
		if (count == SLANG_UNBOUNDED_SIZE || count == SLANG_UNKNOWN_SIZE)
		{
			return 0; // unbounded
		}
		return static_cast<uint32_t>(count);
	}

	bool ClassifyResource(slang::TypeLayoutReflection *typeLayout,
						  slang::ParameterCategory category,
						  ShaderResourceKind &outKind,
						  bool &outIsBuffer,
						  bool &outIsAccelStruct)
	{
		outIsBuffer = false;
		outIsAccelStruct = false;

		slang::TypeReflection *type = typeLayout ? typeLayout->getType() : nullptr;
		const SlangResourceShape shape =
				type ? static_cast<SlangResourceShape>(type->getResourceShape() & SLANG_RESOURCE_BASE_SHAPE_MASK)
					 : SLANG_RESOURCE_NONE;

		switch (category)
		{
			case slang::ParameterCategory::ConstantBuffer:
			case slang::ParameterCategory::PushConstantBuffer:
				outKind = ShaderResourceKind::CBV;
				outIsBuffer = true; // CBVs are root-descriptor eligible
				return true;
			case slang::ParameterCategory::SamplerState:
				outKind = ShaderResourceKind::SAMPLER;
				return true;
			case slang::ParameterCategory::ShaderResource:
				outKind = ShaderResourceKind::SRV;
				break;
			case slang::ParameterCategory::UnorderedAccess:
				outKind = ShaderResourceKind::UAV;
				break;
			default:
				return false;
		}

		if (shape == SLANG_ACCELERATION_STRUCTURE)
		{
			outIsAccelStruct = true;
			outKind = ShaderResourceKind::SRV;
		}
		else if (shape == SLANG_STRUCTURED_BUFFER || shape == SLANG_BYTE_ADDRESS_BUFFER)
		{
			outIsBuffer = true;
		}
		return true;
	}

	void RecordConstantBuffer(const std::string &name,
							  slang::TypeLayoutReflection *bufferTypeLayout,
							  ShaderReflectionData &out)
	{
		if (!bufferTypeLayout)
		{
			return;
		}

		for (const auto &existing: out.constantBuffers)
		{
			if (existing.name == name)
			{
				return;
			}
		}

		slang::TypeLayoutReflection *elementLayout = bufferTypeLayout->getElementTypeLayout();
		if (!elementLayout)
		{
			elementLayout = bufferTypeLayout;
		}

		ReflectedConstantBuffer buffer;
		buffer.name = name;
		buffer.byteSize = static_cast<uint32_t>(elementLayout->getSize(SLANG_PARAMETER_CATEGORY_UNIFORM));

		const unsigned fieldCount = elementLayout->getFieldCount();
		for (unsigned i = 0; i < fieldCount; ++i)
		{
			slang::VariableLayoutReflection *field = elementLayout->getFieldByIndex(i);
			if (!field || !field->getName())
			{
				continue;
			}
			ReflectedConstantBufferVar var;
			var.name = field->getName();
			var.byteOffset = static_cast<uint32_t>(field->getOffset(SLANG_PARAMETER_CATEGORY_UNIFORM));
			var.byteSize =
					field->getTypeLayout()
							? static_cast<uint32_t>(field->getTypeLayout()->getSize(SLANG_PARAMETER_CATEGORY_UNIFORM))
							: 0;
			buffer.variables.push_back(std::move(var));
		}

		out.constantBuffers.push_back(std::move(buffer));
	}

	void AddResource(ShaderReflectionData &out, ReflectedResource resource)
	{
		// The same resource shows up once per stage that uses it; keep the widest binding.
		for (auto &existing: out.resources)
		{
			if (existing.kind == resource.kind && existing.registerSpace == resource.registerSpace &&
				existing.baseShaderRegister == resource.baseShaderRegister)
			{
				if (existing.bindCount != 0 && (resource.bindCount == 0 || resource.bindCount > existing.bindCount))
				{
					existing.bindCount = resource.bindCount;
				}
				existing.used = existing.used || resource.used;
				return;
			}
		}
		out.resources.push_back(std::move(resource));
	}

	// Walks one top-level shader parameter. For DXIL targets Slang lowers each global
	// resource/cbuffer/sampler to its own parameter with a concrete register and space.
	void WalkParameter(slang::VariableLayoutReflection *param, ShaderReflectionData &out)
	{
		if (!param)
		{
			return;
		}

		slang::TypeLayoutReflection *typeLayout = param->getTypeLayout();
		if (!typeLayout)
		{
			return;
		}

		const char *rawName = param->getName();
		const std::string name = rawName ? rawName : "";

		slang::TypeLayoutReflection *leaf = typeLayout->unwrapArray();
		const slang::TypeReflection::Kind kind = leaf ? leaf->getKind() : slang::TypeReflection::Kind::None;

		// ParameterBlock<T> assigns its own register space and packs resources relative to it;
		// the reflected-root-signature builder has no model for that. Shaders targeting this
		// wrapper should use flat register() bindings.
		if (kind == slang::TypeReflection::Kind::ParameterBlock)
		{
			logw("Shader parameter '{}' is a ParameterBlock, which the generated root signature does "
				 "not support; use flat register() bindings instead",
				 name.empty() ? "<unnamed>" : name.c_str());
			return;
		}

		// A constant buffer (named ConstantBuffer<T>, cbuffer {}, or the implicit buffer Slang
		// packs loose top-level uniforms into). Slang gives this a "constantBuffer" binding
		// with a concrete register index.
		if (kind == slang::TypeReflection::Kind::ConstantBuffer)
		{
			const std::string cbufferName = name.empty() ? std::string("$Globals") : name;

			ReflectedResource cbv;
			cbv.name = cbufferName;
			cbv.kind = ShaderResourceKind::CBV;
			cbv.registerSpace = NormalizeRegister(param->getBindingSpace(SLANG_PARAMETER_CATEGORY_CONSTANT_BUFFER));
			cbv.baseShaderRegister = NormalizeRegister(param->getOffset(SLANG_PARAMETER_CATEGORY_CONSTANT_BUFFER));
			if (cbv.registerSpace == UINT32_MAX)
			{
				cbv.registerSpace = NormalizeRegister(param->getBindingSpace());
			}
			if (cbv.baseShaderRegister == UINT32_MAX)
			{
				cbv.baseShaderRegister = NormalizeRegister(param->getBindingIndex());
			}
			cbv.isBuffer = true;
			AddResource(out, std::move(cbv));
			RecordConstantBuffer(cbufferName, leaf, out);
			return;
		}

		// Loose top-level uniforms with no enclosing buffer (rare on D3D targets, but Slang
		// can expose them directly as a "uniform" parameter).
		const slang::ParameterCategory category = param->getCategory();
		if (category == slang::ParameterCategory::Uniform)
		{
			ReflectedResource cbv;
			cbv.name = "$Globals";
			cbv.kind = ShaderResourceKind::CBV;
			cbv.registerSpace = NormalizeRegister(param->getBindingSpace());
			cbv.baseShaderRegister = NormalizeRegister(param->getBindingIndex());
			cbv.isBuffer = true;
			AddResource(out, std::move(cbv));
			RecordConstantBuffer("$Globals", typeLayout, out);
			return;
		}

		ShaderResourceKind resourceKind = ShaderResourceKind::SRV;
		bool isBuffer = false;
		bool isAccelStruct = false;
		if (!ClassifyResource(leaf, category, resourceKind, isBuffer, isAccelStruct))
		{
			return;
		}

		ReflectedResource resource;
		resource.name = name;
		resource.kind = resourceKind;
		resource.registerSpace = NormalizeRegister(param->getBindingSpace(category));
		resource.baseShaderRegister = NormalizeRegister(param->getOffset(category));
		if (resource.registerSpace == UINT32_MAX)
		{
			resource.registerSpace = NormalizeRegister(param->getBindingSpace());
		}
		if (resource.baseShaderRegister == UINT32_MAX)
		{
			resource.baseShaderRegister = NormalizeRegister(param->getBindingIndex());
		}
		resource.bindCount = BindCountFromTypeLayout(typeLayout);
		resource.isBuffer = isBuffer;
		resource.isAccelerationStructure = isAccelStruct;
		AddResource(out, std::move(resource));
	}

	void FillReflection(slang::ProgramLayout *layout, ShaderReflectionData &out)
	{
		if (!layout)
		{
			return;
		}

		const bool bindless = layout->getBindlessSpaceIndex() >= 0;
		out.usesResourceDescriptorHeapIndexing = bindless;
		out.usesSamplerDescriptorHeapIndexing = bindless;

		const unsigned parameterCount = layout->getParameterCount();
		for (unsigned i = 0; i < parameterCount; ++i)
		{
			WalkParameter(layout->getParameterByIndex(i), out);
		}

		// Entry-point uniform parameters (a cbuffer declared on the entry point, etc.).
		const SlangUInt entryPointCount = layout->getEntryPointCount();
		for (SlangUInt e = 0; e < entryPointCount; ++e)
		{
			slang::EntryPointReflection *entryPoint = layout->getEntryPointByIndex(e);
			if (!entryPoint)
			{
				continue;
			}
			const unsigned epParams = entryPoint->getParameterCount();
			for (unsigned p = 0; p < epParams; ++p)
			{
				WalkParameter(entryPoint->getParameterByIndex(p), out);
			}
		}
	}

	// After code generation, ask Slang which reflected locations the compiled shader actually
	// references. DXC pruned unused resources implicitly; this restores that behavior.
	void ApplyUsedFilter(slang::IMetadata *metadata, ShaderReflectionData &out)
	{
		if (!metadata)
		{
			return;
		}
		for (auto &resource: out.resources)
		{
			if (resource.registerSpace == UINT32_MAX || resource.baseShaderRegister == UINT32_MAX)
			{
				continue;
			}
			bool used = true;
			if (SLANG_SUCCEEDED(metadata->isParameterLocationUsed(CategoryForKind(resource.kind),
																  resource.registerSpace,
																  resource.baseShaderRegister,
																  used)))
			{
				resource.used = used;
			}
		}
	}
} // namespace

struct SlangShaderCompiler::Impl
{
	ComPtr<slang::IGlobalSession> globalSession;

	// Builds the shared compiler-option list for one compile invocation. `storage` keeps the
	// backing strings alive for the lifetime of the returned entries (deque => stable c_str()).
	std::vector<slang::CompilerOptionEntry> BuildOptions(const std::vector<std::wstring> &arguments,
														 std::deque<std::string> &storage) const
	{
		std::vector<slang::CompilerOptionEntry> options;

		auto pushString = [&](slang::CompilerOptionName name, std::string value0, std::string value1)
		{
			storage.push_back(std::move(value0));
			const char *s0 = storage.back().c_str();
			const char *s1 = nullptr;
			if (!value1.empty())
			{
				storage.push_back(std::move(value1));
				s1 = storage.back().c_str();
			}
			slang::CompilerOptionEntry entry{};
			entry.name = name;
			entry.value.kind = slang::CompilerOptionValueKind::String;
			entry.value.stringValue0 = s0;
			entry.value.stringValue1 = s1;
			options.push_back(entry);
		};

		auto pushInt = [&](slang::CompilerOptionName name, int32_t value)
		{
			slang::CompilerOptionEntry entry{};
			entry.name = name;
			entry.value.kind = slang::CompilerOptionValueKind::Int;
			entry.value.intValue0 = value;
			options.push_back(entry);
		};

#ifdef _DEBUG
		pushInt(slang::CompilerOptionName::Optimization, SLANG_OPTIMIZATION_LEVEL_NONE);
		pushInt(slang::CompilerOptionName::DebugInformation, SLANG_DEBUG_INFO_LEVEL_MAXIMAL);
#else
		pushInt(slang::CompilerOptionName::Optimization, SLANG_OPTIMIZATION_LEVEL_HIGH);
		pushInt(slang::CompilerOptionName::DebugInformation, SLANG_DEBUG_INFO_LEVEL_NONE);
#endif

		for (const auto &arg: arguments)
		{
			if (arg.empty())
			{
				continue;
			}
			const std::string narrow = Narrow(arg);
			if (narrow.front() == '-' || narrow.front() == '/')
			{
				// Legacy DXC flags that have no Slang equivalent (16-bit types are implicit at
				// sm_6_6, root signatures are always reflected, RT payloads are native).
				if (narrow.rfind("-enable-16bit-types", 0) == 0 || narrow.rfind("-HV", 0) == 0 ||
					narrow.rfind("-enable-payload-qualifiers", 0) == 0 || narrow.rfind("-rootsig-define", 0) == 0 ||
					narrow.rfind("-Qembed_debug", 0) == 0 || narrow.rfind("-Zi", 0) == 0)
				{
					logd("Ignoring DXC-only compiler flag '{}' under Slang", narrow.c_str());
					continue;
				}
				logw("Unrecognized compiler flag '{}' passed to the Slang compiler; ignoring", narrow.c_str());
				continue;
			}

			// Bare token => a preprocessor define, optionally NAME=VALUE.
			const size_t eq = narrow.find('=');
			if (eq == std::string::npos)
			{
				pushString(slang::CompilerOptionName::MacroDefine, narrow, {});
			}
			else
			{
				pushString(slang::CompilerOptionName::MacroDefine, narrow.substr(0, eq), narrow.substr(eq + 1));
			}
		}

		return options;
	}
};

SlangShaderCompiler::SlangShaderCompiler() : impl_(std::make_unique<Impl>())
{
	if (SLANG_FAILED(slang::createGlobalSession(impl_->globalSession.writeRef())))
	{
		throw std::runtime_error("Failed to create Slang global session.");
	}
}

SlangShaderCompiler::~SlangShaderCompiler() = default;

ShaderCompilationResult SlangShaderCompiler::CompileShaderFromFile(const std::wstring &shaderPath,
																   const std::wstring &entryPoint,
																   const std::wstring &targetProfile,
																   const std::vector<std::wstring> &arguments)
{
	const ShaderEntryPoint single{ entryPoint, targetProfile };
	return CompileShaderFromFile(shaderPath, std::span<const ShaderEntryPoint>(&single, 1), arguments);
}

ShaderCompilationResult SlangShaderCompiler::CompileShaderFromFile(const std::wstring &shaderPath,
																   std::span<const ShaderEntryPoint> entryPoints,
																   const std::vector<std::wstring> &arguments)
{
	ShaderCompilationResult result;

	if (entryPoints.empty())
	{
		logw("CompileShaderFromFile called with no entry points");
		return result;
	}

	// Read the shader file.
	std::ifstream shaderFile(shaderPath, std::ios::binary | std::ios::ate);
	if (!shaderFile.is_open())
	{
		logw("Failed to open shader file: {}", Narrow(shaderPath).c_str());
		return result;
	}

	const std::streamsize fileSize = shaderFile.tellg();
	shaderFile.seekg(0, std::ios::beg);

	std::string shaderSource(static_cast<size_t>(fileSize), '\0');
	if (fileSize > 0 && !shaderFile.read(shaderSource.data(), fileSize))
	{
		logw("Failed to read shader file: {}", Narrow(shaderPath).c_str());
		return result;
	}
	shaderFile.close();

	const std::string pathUtf8 = Narrow(shaderPath);
	const size_t slash = pathUtf8.find_last_of("\\/");
	const std::string shaderDir = slash == std::string::npos ? std::string(".") : pathUtf8.substr(0, slash);
	std::string moduleName = slash == std::string::npos ? pathUtf8 : pathUtf8.substr(slash + 1);
	if (const size_t dot = moduleName.find_last_of('.'); dot != std::string::npos)
	{
		moduleName = moduleName.substr(0, dot);
	}

	// A "library" target is a whole-program compile of every entry point into one DXIL blob.
	bool anyLibrary = false;
	for (const auto &entry: entryPoints)
	{
		if (ParseTargetProfile(entry.targetProfile).isLibrary)
		{
			anyLibrary = true;
			break;
		}
	}

	const ParsedTargetProfile firstProfile = ParseTargetProfile(entryPoints.front().targetProfile);

	std::deque<std::string> optionStorage;
	std::vector<slang::CompilerOptionEntry> options = impl_->BuildOptions(arguments, optionStorage);

	slang::TargetDesc targetDesc{};
	targetDesc.format = SLANG_DXIL;
	targetDesc.profile = impl_->globalSession->findProfile(firstProfile.profile.c_str());
	if (anyLibrary)
	{
		targetDesc.flags |= SLANG_TARGET_FLAG_GENERATE_WHOLE_PROGRAM;
	}

	const char *searchPaths[] = { shaderDir.c_str() };

	slang::SessionDesc sessionDesc{};
	sessionDesc.targets = &targetDesc;
	sessionDesc.targetCount = 1;
	sessionDesc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_ROW_MAJOR;
	sessionDesc.searchPaths = searchPaths;
	sessionDesc.searchPathCount = 1;
	sessionDesc.compilerOptionEntries = options.empty() ? nullptr : options.data();
	sessionDesc.compilerOptionEntryCount = static_cast<uint32_t>(options.size());

	ComPtr<slang::ISession> session;
	if (SLANG_FAILED(impl_->globalSession->createSession(sessionDesc, session.writeRef())))
	{
		logw("Failed to create Slang session");
		return result;
	}

	// The module is owned by the session and stays valid for as long as the session does.
	ComPtr<slang::IBlob> moduleDiagnostics;
	slang::IModule *module = nullptr;
	{
		ComPtr<slang::IBlob> sourceBlob(slang_createBlob(shaderSource.data(), shaderSource.size()));
		module = session->loadModuleFromSource(moduleName.c_str(),
											   pathUtf8.c_str(),
											   sourceBlob,
											   moduleDiagnostics.writeRef());
	}
	LogDiagnostics(moduleDiagnostics);
	if (!module)
	{
		logw("Shader compilation failed while loading module");
		return result;
	}

	// Resolve each requested entry point and build the component list (module first). The
	// ComPtr elements take their own reference; the underlying objects outlive the session
	// only if something keeps them alive, which nothing here does past this function.
	std::vector<ComPtr<slang::IComponentType>> components;
	components.emplace_back(module);

	std::vector<ShaderStage> entryStages;
	std::vector<std::string> entryNames;
	for (const auto &entry: entryPoints)
	{
		const ParsedTargetProfile parsed = ParseTargetProfile(entry.targetProfile);
		std::string entryName = Narrow(entry.entryPoint);
		if (entryName.empty())
		{
			entryName = "main";
		}

		ComPtr<slang::IEntryPoint> entryPointComponent;
		ComPtr<slang::IBlob> entryDiag;
		SlangResult hr;
		if (!parsed.isLibrary && parsed.slangStage != SLANG_STAGE_NONE)
		{
			hr = module->findAndCheckEntryPoint(entryName.c_str(),
												parsed.slangStage,
												entryPointComponent.writeRef(),
												entryDiag.writeRef());
		}
		else
		{
			// Library / raytracing: the stage comes from the [shader("...")] attribute.
			hr = module->findEntryPointByName(entryName.c_str(), entryPointComponent.writeRef());
		}
		LogDiagnostics(entryDiag);
		if (SLANG_FAILED(hr) || !entryPointComponent)
		{
			logw("Failed to find shader entry point '{}'", entryName.c_str());
			return result;
		}

		components.emplace_back(entryPointComponent);
		entryStages.push_back(parsed.isLibrary ? ShaderStage::LIBRARY : parsed.stage);
		entryNames.push_back(entryName);
	}

	std::vector<slang::IComponentType *> rawComponents;
	rawComponents.reserve(components.size());
	for (auto &component: components)
	{
		rawComponents.push_back(component.get());
	}

	ComPtr<slang::IComponentType> composed;
	{
		ComPtr<slang::IBlob> composeDiag;
		const SlangResult hr = session->createCompositeComponentType(rawComponents.data(),
																	 static_cast<SlangInt>(rawComponents.size()),
																	 composed.writeRef(),
																	 composeDiag.writeRef());
		LogDiagnostics(composeDiag);
		if (SLANG_FAILED(hr) || !composed)
		{
			logw("Failed to compose shader program");
			return result;
		}
	}

	ComPtr<slang::IComponentType> linked;
	{
		ComPtr<slang::IBlob> linkDiag;
		const SlangResult hr = composed->link(linked.writeRef(), linkDiag.writeRef());
		LogDiagnostics(linkDiag);
		if (SLANG_FAILED(hr) || !linked)
		{
			logw("Failed to link shader program");
			return result;
		}
	}

	slang::ProgramLayout *programLayout = nullptr;
	{
		ComPtr<slang::IBlob> layoutDiag;
		programLayout = linked->getLayout(0, layoutDiag.writeRef());
		LogDiagnostics(layoutDiag);
	}

	ShaderReflectionData sharedReflection;
	FillReflection(programLayout, sharedReflection);

	if (anyLibrary)
	{
		ComPtr<slang::IBlob> codeDiag;
		ComPtr<slang::IBlob> code;
		const SlangResult hr = linked->getTargetCode(0, code.writeRef(), codeDiag.writeRef());
		LogDiagnostics(codeDiag);
		if (SLANG_FAILED(hr) || !code)
		{
			logw("Failed to generate DXIL library");
			return result;
		}

		CompiledShader shader;
		shader.stage = ShaderStage::LIBRARY;
		shader.bytecode.assign(static_cast<const uint8_t *>(code->getBufferPointer()),
							   static_cast<const uint8_t *>(code->getBufferPointer()) + code->getBufferSize());
		shader.reflection = sharedReflection;

		ComPtr<slang::IMetadata> metadata;
		ComPtr<slang::IBlob> metaDiag;
		if (SLANG_SUCCEEDED(linked->getTargetMetadata(0, metadata.writeRef(), metaDiag.writeRef())))
		{
			ApplyUsedFilter(metadata, shader.reflection);
		}
		LogDiagnostics(metaDiag);

		result.shaders.push_back(std::move(shader));
		result.success = true;
		return result;
	}

	// One CompiledShader per entry point. Entry point i sits at component index i+1, so its
	// index within the linked program is i.
	for (size_t i = 0; i < entryNames.size(); ++i)
	{
		ComPtr<slang::IBlob> codeDiag;
		ComPtr<slang::IBlob> code;
		const SlangResult hr =
				linked->getEntryPointCode(static_cast<SlangInt>(i), 0, code.writeRef(), codeDiag.writeRef());
		LogDiagnostics(codeDiag);
		if (SLANG_FAILED(hr) || !code)
		{
			logw("Failed to generate DXIL for entry point '{}'", entryNames[i].c_str());
			return result;
		}

		CompiledShader shader;
		shader.stage = entryStages[i];
		shader.entryPoint = entryNames[i];
		shader.bytecode.assign(static_cast<const uint8_t *>(code->getBufferPointer()),
							   static_cast<const uint8_t *>(code->getBufferPointer()) + code->getBufferSize());
		shader.reflection = sharedReflection;

		ComPtr<slang::IMetadata> metadata;
		ComPtr<slang::IBlob> metaDiag;
		if (SLANG_SUCCEEDED(linked->getEntryPointMetadata(static_cast<SlangInt>(i),
														  0,
														  metadata.writeRef(),
														  metaDiag.writeRef())))
		{
			ApplyUsedFilter(metadata, shader.reflection);
		}
		LogDiagnostics(metaDiag);

		result.shaders.push_back(std::move(shader));
	}

	result.success = true;
	return result;
}
