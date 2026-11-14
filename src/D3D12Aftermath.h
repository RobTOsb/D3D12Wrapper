#pragma once

#include "pch.h"

// NVIDIA Nsight Aftermath GPU crash dumps, when the wrapper is built with D3D12_AFTERMATH
namespace Aftermath
{
	// Before any D3D12 device is made, or the device is not watched.
	void EnableGpuCrashDumps();

	// Right after the device is made: the per-device features (shader debug information, shader error
	// reporting, resource tracking, event markers).
	void InitializeDevice(ID3D12Device *device);

	// Every shader binary handed to D3D12, so a dump's shaders can be matched to their DXIL and source.
	void RegisterShaderBinary(const void *bytecode, size_t size);

	// After the device is removed: waits (a few seconds at most) until Aftermath has written the dump.
	void WaitForGpuCrashDump();
}
