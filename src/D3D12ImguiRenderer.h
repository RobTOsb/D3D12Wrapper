#pragma once

#include "pch.h"

class D3D12Device;
class D3D12Swapchain;
class D3D12DescriptorHeap;
class D3D12CommandList;
class D3D12CommandQueue;

class D3D12ImguiRenderer
{
public:
	D3D12ImguiRenderer(D3D12Device *device,
					   DXGI_FORMAT rtvFormat,
					   D3D12CommandQueue *commandQueue,
					   D3D12DescriptorHeap *globalDescriptorHeap,
					   void *hwnd,
					   uint32_t frameCount);
	~D3D12ImguiRenderer();

	void NewFrame()
	{
		ImGui_ImplDX12_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();
	}

	void RenderUI(D3D12CommandList *commandList);

private:
	D3D12DescriptorHeap *descriptorHeap_ = nullptr;
	DescriptorHandle fontDescriptorHandle_ = InvalidDescriptorHandle;
};
