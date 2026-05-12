#pragma once

#include "directx/d3d12.h"

#include <d3d11on12.h>

#include "directx/d3dx12.h"

#include "directx/d3d12sdklayers.h"

#include <dxcapi.h>

#include "D3D12MemAlloc.h"

#include "backends/imgui_impl_win32.h"

#include "backends/imgui_impl_dx12.h"

#include "imgui.h"

#include "imgui_spectrum.h"

#include "dxgi1_6.h"

#include "HelperClasses/RenderTargetHelper.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <wrl.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using CPUDescriptorHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE;
using GPUDescriptorHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE;
using D3D12CBVDesc = D3D12_CONSTANT_BUFFER_VIEW_DESC;
using D3D12SRVDesc = D3D12_SHADER_RESOURCE_VIEW_DESC;
using D3D12UAVDesc = D3D12_UNORDERED_ACCESS_VIEW_DESC;
using ResourceBarrier = CD3DX12_RESOURCE_BARRIER;
using D3D12TextureBarrier = D3D12_TEXTURE_BARRIER;
using D3D12BufferBarrier = D3D12_BUFFER_BARRIER;
using D3D12GlobalBarrier = D3D12_GLOBAL_BARRIER;
using D3D12BarrierGroup = CD3DX12_BARRIER_GROUP;
using D3D12BarrierSubresourceRange = D3D12_BARRIER_SUBRESOURCE_RANGE;
using D3D12Rect = D3D12_RECT;
using D3D12ViewPort = D3D12_VIEWPORT;
using D3D12ResourceDesc = CD3DX12_RESOURCE_DESC;
using D3D12ResourceDesc1 = CD3DX12_RESOURCE_DESC1;

using DescriptorHandle = uint32_t;

static constexpr DescriptorHandle InvalidDescriptorHandle = UINT32_MAX;
