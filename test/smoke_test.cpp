#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "D3D12Device.h"
#include "D3D12Pipeline.h"
#include "D3D12RootSignatureBuilder.h"
#include "SlangCompiler.h"

namespace
{
	int g_failures = 0;

	void Check(bool condition, const char *what)
	{
		std::printf("  [%s] %s\n", condition ? "PASS" : "FAIL", what);
		if (!condition)
		{
			++g_failures;
		}
	}

	void Skip(const char *what)
	{
		std::printf("  [SKIP] %s\n", what);
	}

	bool DeviceSupportsMeshShaders(ID3D12Device *device)
	{
		D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7 = {};
		if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &options7, sizeof(options7))))
		{
			return false;
		}
		return options7.MeshShaderTier != D3D12_MESH_SHADER_TIER_NOT_SUPPORTED;
	}

	const ReflectedResource *FindResource(const ShaderReflectionData &reflection,
										  ShaderResourceKind kind,
										  uint32_t reg,
										  uint32_t space = 0)
	{
		for (const auto &resource: reflection.resources)
		{
			if (resource.kind == kind && resource.baseShaderRegister == reg && resource.registerSpace == space)
			{
				return &resource;
			}
		}
		return nullptr;
	}

	bool HasDxilContainer(const std::vector<uint8_t> &bytecode)
	{
		// A DXC/Slang DXIL container starts with the 'DXBC' FourCC.
		return bytecode.size() > 4 && bytecode[0] == 'D' && bytecode[1] == 'X' && bytecode[2] == 'B' &&
			   bytecode[3] == 'C';
	}

	std::wstring ShaderPath(const wchar_t *name)
	{
		return std::wstring(L"shaders/") + name;
	}
} // namespace

int main()
{
	SlangShaderCompiler compiler;

	// Graphics + compute
	std::printf("smoke.slang (vs/ps/cs)\n");
	const std::vector<ShaderEntryPoint> graphicsEntries = {
		{ L"vsMain", L"vs_6_6" },
		{ L"psMain", L"ps_6_6" },
		{ L"csMain", L"cs_6_6" },
	};
	ShaderCompilationResult smoke = compiler.CompileShaderFromFile(ShaderPath(L"smoke.slang"), graphicsEntries);

	Check(smoke.success, "compilation succeeded");
	Check(smoke.Has(ShaderStage::VERTEX), "has vertex stage");
	Check(smoke.Has(ShaderStage::PIXEL), "has pixel stage");
	Check(smoke.Has(ShaderStage::COMPUTE), "has compute stage");

	if (const CompiledShader *vs = smoke.Find(ShaderStage::VERTEX))
	{
		Check(HasDxilContainer(vs->bytecode), "vertex bytecode is a DXIL container");
	}
	if (const CompiledShader *ps = smoke.Find(ShaderStage::PIXEL))
	{
		Check(HasDxilContainer(ps->bytecode), "pixel bytecode is a DXIL container");

		bool foundPushConstants = false;
		for (const auto &cbuffer: ps->reflection.constantBuffers)
		{
			if (cbuffer.name == "g_PushConstants")
			{
				foundPushConstants = true;
				Check(cbuffer.byteSize >= 16, "g_PushConstants cbuffer is at least 16 bytes");
			}
		}
		Check(foundPushConstants, "g_PushConstants cbuffer reflected");

		Check(FindResource(ps->reflection, ShaderResourceKind::SRV, 0) != nullptr, "SRV t at t0 reflected");
		Check(FindResource(ps->reflection, ShaderResourceKind::SAMPLER, 0) != nullptr, "sampler s at s0 reflected");
	}
	if (const CompiledShader *cs = smoke.Find(ShaderStage::COMPUTE))
	{
		Check(HasDxilContainer(cs->bytecode), "compute bytecode is a DXIL container");
		const ReflectedResource *uav = FindResource(cs->reflection, ShaderResourceKind::UAV, 0);
		Check(uav != nullptr, "UAV outBuffer at u0 reflected");
		if (uav)
		{
			Check(uav->isBuffer, "outBuffer is classified as a buffer (root-descriptor eligible)");
		}
	}

	D3D12Device device(/*useSoftwareAdapter=*/true);

	std::printf("graphics pipeline\n");
	try
	{
		auto graphicsPipeline = device.CreateGraphicsPipeline();
		graphicsPipeline->BuildRootSignatureFromShader(smoke);
		Check(graphicsPipeline->GetNativeRootSignature() != nullptr, "graphics root signature created");

		GraphicsPipelineCreateInfo info;
		info.pipelineType = PipelineType::GRAPHICS_VERTEX;
		info.rtvFormats = { DXGI_FORMAT_R8G8B8A8_UNORM };
		info.depthStencilState.depthEnable = FALSE;
		graphicsPipeline->BuildGraphicsPipeline(info, smoke);
		Check(graphicsPipeline->GetNativePipelineState() != nullptr, "graphics pipeline state created");
	}
	catch (const std::exception &e)
	{
		std::printf("  [FAIL] graphics pipeline threw: %s\n", e.what());
		++g_failures;
	}

	std::printf("compute pipeline\n");
	try
	{
		auto computePipeline = device.CreateComputePipeline();
		computePipeline->BuildRootSignatureFromShader(smoke);
		Check(computePipeline->GetNativeRootSignature() != nullptr, "compute root signature created");
		computePipeline->BuildComputePipeline(smoke);
		Check(computePipeline->GetNativePipelineState() != nullptr, "compute pipeline state created");
	}
	catch (const std::exception &e)
	{
		std::printf("  [FAIL] compute pipeline threw: %s\n", e.what());
		++g_failures;
	}

	// Mesh + amplification
	std::printf("mesh.slang (as/ms/ps)\n");
	const std::vector<ShaderEntryPoint> meshEntries = {
		{ L"asMain", L"as_6_6" },
		{ L"msMain", L"ms_6_6" },
		{ L"psMain", L"ps_6_6" },
	};
	ShaderCompilationResult mesh = compiler.CompileShaderFromFile(ShaderPath(L"mesh.slang"), meshEntries);

	Check(mesh.success, "mesh compilation succeeded");
	Check(mesh.Has(ShaderStage::AMPLIFICATION), "has amplification stage");
	Check(mesh.Has(ShaderStage::MESH), "has mesh stage");
	Check(mesh.Has(ShaderStage::PIXEL), "has pixel stage");
	if (const CompiledShader *as = mesh.Find(ShaderStage::AMPLIFICATION))
	{
		Check(HasDxilContainer(as->bytecode), "amplification bytecode is a DXIL container");
	}
	if (const CompiledShader *ms = mesh.Find(ShaderStage::MESH))
	{
		Check(HasDxilContainer(ms->bytecode), "mesh bytecode is a DXIL container");

		bool foundPushConstants = false;
		for (const auto &cbuffer: ms->reflection.constantBuffers)
		{
			if (cbuffer.name == "g_PushConstants")
			{
				foundPushConstants = true;
			}
		}
		Check(foundPushConstants, "g_PushConstants cbuffer reflected from mesh stage");
	}

	std::printf("mesh shader pipeline\n");
	if (!DeviceSupportsMeshShaders(device.GetNativeDevice().Get()))
	{
		Skip("mesh shaders not supported on this adapter");
	}
	else
	{
		try
		{
			auto meshPipeline = device.CreateGraphicsPipeline();
			meshPipeline->BuildRootSignatureFromShader(mesh);
			Check(meshPipeline->GetNativeRootSignature() != nullptr, "mesh root signature created");

			GraphicsPipelineCreateInfo info;
			info.pipelineType = PipelineType::GRAPHICS_MESH;
			info.rtvFormats = { DXGI_FORMAT_R8G8B8A8_UNORM };
			info.depthStencilState.depthEnable = FALSE;
			meshPipeline->BuildGraphicsPipeline(info, mesh);
			Check(meshPipeline->GetNativePipelineState() != nullptr, "mesh pipeline state created");
		}
		catch (const std::exception &e)
		{
			std::printf("  [FAIL] mesh shader pipeline threw: %s\n", e.what());
			++g_failures;
		}
	}

	// Raytracing library
	std::printf("rt.slang (raygen/miss/closesthit library)\n");
	const std::vector<ShaderEntryPoint> rtEntries = {
		{ L"rgen", L"lib_6_6" },
		{ L"miss", L"lib_6_6" },
		{ L"chit", L"lib_6_6" },
	};
	ShaderCompilationResult rt = compiler.CompileShaderFromFile(ShaderPath(L"rt.slang"), rtEntries);

	Check(rt.success, "rt compilation succeeded");
	Check(rt.shaders.size() == 1, "rt produced a single library blob");
	Check(rt.Has(ShaderStage::LIBRARY), "rt has a LIBRARY shader");
	if (const CompiledShader *lib = rt.Find(ShaderStage::LIBRARY))
	{
		Check(HasDxilContainer(lib->bytecode), "library bytecode is a DXIL container");
		Check(FindResource(lib->reflection, ShaderResourceKind::SRV, 0) != nullptr,
			  "acceleration structure at t0 reflected");
		Check(FindResource(lib->reflection, ShaderResourceKind::UAV, 0) != nullptr, "output image UAV at u0 reflected");
	}

	std::printf("raytracing pipeline\n");
	try
	{
		auto rtPipeline = device.CreateRaytracingPipeline();
		rtPipeline->BuildRootSignatureFromShader(rt);
		Check(rtPipeline->GetNativeRootSignature() != nullptr, "raytracing root signature created");

		RaytracingPipelineCreateInfo info;
		info.exports = { L"rgen", L"miss", L"chit" };
		RaytracingHitGroupDesc hitGroup;
		hitGroup.hitGroupExport = L"hitGroup";
		hitGroup.closestHitExport = L"chit";
		hitGroup.type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
		info.hitGroups = { hitGroup };
		info.maxPayloadSizeInBytes = sizeof(float) * 3;
		info.maxAttributeSizeInBytes = sizeof(float) * 2;
		info.maxTraceRecursionDepth = 1;

		rtPipeline->BuildRaytracingPipeline(info, rt);
		Check(rtPipeline->GetNativeStateObject() != nullptr, "raytracing state object created");

		if (auto props = rtPipeline->GetStateObjectProperties())
		{
			Check(props->GetShaderIdentifier(L"rgen") != nullptr, "raygen export 'rgen' resolvable (name not mangled)");
			Check(props->GetShaderIdentifier(L"miss") != nullptr, "miss export 'miss' resolvable");
			Check(props->GetShaderIdentifier(L"hitGroup") != nullptr, "hit group 'hitGroup' resolvable");
		}
	}
	catch (const std::exception &e)
	{
		std::printf("  [FAIL] raytracing pipeline threw: %s\n", e.what());
		++g_failures;
	}

	std::printf("\n%s (%d failure%s)\n", g_failures == 0 ? "OK" : "FAILED", g_failures, g_failures == 1 ? "" : "s");
	return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
