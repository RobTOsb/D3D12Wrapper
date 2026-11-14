#include "D3D12Aftermath.h"

#ifdef D3D12_AFTERMATH

#include <GFSDK_Aftermath.h>
#include <GFSDK_Aftermath_GpuCrashDump.h>
#include <GFSDK_Aftermath_GpuCrashDumpDecoding.h>

#include <fmtlog.h>

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

namespace
{
	std::mutex g_mutex;
	bool g_enabled = false;
	std::filesystem::path g_directory;
	std::string g_applicationName;
	// Shader binaries already on disk this run, by Aftermath shader hash.
	std::unordered_set<uint64_t> g_writtenShaders;

	std::filesystem::path ExecutableDirectory(std::string &stem)
	{
		wchar_t path[MAX_PATH] = {};
		GetModuleFileNameW(nullptr, path, MAX_PATH);
		const std::filesystem::path executable(path);
		stem = executable.stem().string();
		return executable.parent_path();
	}

	void WriteFile(const std::filesystem::path &path, const void *data, size_t size)
	{
		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		if (!file)
		{
			loge("Aftermath: could not write {}", path.string());
			return;
		}
		file.write(static_cast<const char *>(data), static_cast<std::streamsize>(size));
	}

	std::string Timestamp()
	{
		const std::time_t now = std::time(nullptr);
		std::tm local = {};
		localtime_s(&local, &now);
		char text[32];
		std::strftime(text, sizeof(text), "%Y%m%d-%H%M%S", &local);
		return text;
	}

	void GFSDK_AFTERMATH_CALL OnGpuCrashDump(const void *dump, const uint32_t size, void *)
	{
		std::lock_guard lock(g_mutex);
		const std::filesystem::path path = g_directory / (g_applicationName + "-" + Timestamp() + ".nv-gpudmp");
		WriteFile(path, dump, size);
		loge("Aftermath: GPU crash dump written to {}", path.string());
	}

	void GFSDK_AFTERMATH_CALL OnShaderDebugInfo(const void *debugInfo, const uint32_t size, void *)
	{
		std::lock_guard lock(g_mutex);
		GFSDK_Aftermath_ShaderDebugInfoIdentifier identifier = {};
		if (!GFSDK_Aftermath_SUCCEED(
					GFSDK_Aftermath_GetShaderDebugInfoIdentifier(GFSDK_Aftermath_Version_API, debugInfo, size, &identifier)))
		{
			return;
		}
		// The file name Nsight's own crash dump monitor uses.
		char name[64];
		std::snprintf(name, sizeof(name), "%016llX-%016llX.nvdbg", static_cast<unsigned long long>(identifier.id[0]),
					  static_cast<unsigned long long>(identifier.id[1]));
		WriteFile(g_directory / name, debugInfo, size);
	}

	void GFSDK_AFTERMATH_CALL OnDescription(PFN_GFSDK_Aftermath_AddGpuCrashDumpDescription addDescription, void *)
	{
		addDescription(GFSDK_Aftermath_GpuCrashDumpDescriptionKey_ApplicationName, g_applicationName.c_str());
#ifdef _DEBUG
		addDescription(GFSDK_Aftermath_GpuCrashDumpDescriptionKey_UserDefined, "Debug build");
#else
		addDescription(GFSDK_Aftermath_GpuCrashDumpDescriptionKey_UserDefined, "Release build");
#endif
	}
}

namespace Aftermath
{
	void EnableGpuCrashDumps()
	{
		std::lock_guard lock(g_mutex);
		g_directory = ExecutableDirectory(g_applicationName) / "GpuCrashDumps";
		std::error_code error;
		std::filesystem::create_directories(g_directory, error);

		// Deferred: the driver keeps the shader debug information and hands over only that of the shaders in a dump.
		const GFSDK_Aftermath_Result result =
				GFSDK_Aftermath_EnableGpuCrashDumps(GFSDK_Aftermath_Version_API,
													GFSDK_Aftermath_GpuCrashDumpWatchedApiFlags_DX,
													GFSDK_Aftermath_GpuCrashDumpFeatureFlags_DeferDebugInfoCallbacks,
													OnGpuCrashDump,
													OnShaderDebugInfo,
													OnDescription,
													nullptr,
													nullptr);
		g_enabled = GFSDK_Aftermath_SUCCEED(result);
		if (g_enabled)
			logi("Aftermath: GPU crash dumps enabled, written to {}", g_directory.string());
		else
			logw("Aftermath: GPU crash dumps unavailable (result 0x{:08X})", static_cast<uint32_t>(result));
	}

	void InitializeDevice(ID3D12Device *device)
	{
		if (!g_enabled)
			return;
		// Not call stack capturing: it needs the crash dump monitor running and costs a lot of CPU per command.
		const uint32_t flags = GFSDK_Aftermath_FeatureFlags_EnableMarkers | GFSDK_Aftermath_FeatureFlags_EnableResourceTracking |
							   GFSDK_Aftermath_FeatureFlags_GenerateShaderDebugInfo |
							   GFSDK_Aftermath_FeatureFlags_EnableShaderErrorReporting;
		const GFSDK_Aftermath_Result result = GFSDK_Aftermath_DX12_Initialize(GFSDK_Aftermath_Version_API, flags, device);
		if (GFSDK_Aftermath_SUCCEED(result))
			logi("Aftermath: device features enabled (shader debug info, shader error reporting, resource tracking)");
		else
			logw("Aftermath: device features unavailable (result 0x{:08X}); dumps are still written, with less in them",
				 static_cast<uint32_t>(result));
	}

	void RegisterShaderBinary(const void *bytecode, size_t size)
	{
		if (!g_enabled || bytecode == nullptr || size == 0)
			return;
		const D3D12_SHADER_BYTECODE shader = { bytecode, size };
		GFSDK_Aftermath_ShaderBinaryHash hash = {};
		if (!GFSDK_Aftermath_SUCCEED(GFSDK_Aftermath_GetShaderHash(GFSDK_Aftermath_Version_API, &shader, &hash)))
			return;

		std::lock_guard lock(g_mutex);
		if (!g_writtenShaders.insert(hash.hash).second)
			return;
		char name[32];
		std::snprintf(name, sizeof(name), "%016llX.dxil", static_cast<unsigned long long>(hash.hash));
		const std::filesystem::path path = g_directory / name;
		std::error_code error;
		if (!std::filesystem::exists(path, error))
			WriteFile(path, bytecode, size);
	}

	void WaitForGpuCrashDump()
	{
		if (!g_enabled)
			return;
		using Clock = std::chrono::steady_clock;
		const Clock::time_point start = Clock::now();
		GFSDK_Aftermath_CrashDump_Status status = GFSDK_Aftermath_CrashDump_Status_Unknown;
		GFSDK_Aftermath_GetCrashDumpStatus(&status);
		while (status != GFSDK_Aftermath_CrashDump_Status_CollectingDataFailed &&
			   status != GFSDK_Aftermath_CrashDump_Status_Finished && Clock::now() - start < std::chrono::seconds(10))
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
			GFSDK_Aftermath_GetCrashDumpStatus(&status);
		}
		if (status == GFSDK_Aftermath_CrashDump_Status_Finished)
			loge("Aftermath: crash dump finished; open it in Nsight Graphics with {} as the shader search path",
				 g_directory.string());
		else if (status == GFSDK_Aftermath_CrashDump_Status_NotStarted)
			loge("Aftermath: no crash dump was started for this device removal");
		else
			loge("Aftermath: crash dump did not finish (status {})", static_cast<int>(status));
	}
}

#else

namespace Aftermath
{
	void EnableGpuCrashDumps() {}
	void InitializeDevice(ID3D12Device *) {}
	void RegisterShaderBinary(const void *, size_t) {}
	void WaitForGpuCrashDump() {}
}

#endif
