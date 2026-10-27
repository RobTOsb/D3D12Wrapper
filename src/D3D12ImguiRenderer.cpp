#include "D3D12ImguiRenderer.h"

#include "D3D12CommandList.h"
#include "D3D12CommandQueue.h"
#include "D3D12DescriptorHeap.h"
#include "D3D12Device.h"

D3D12ImguiRenderer::D3D12ImguiRenderer(D3D12Device *device,
									   DXGI_FORMAT rtvFormat,
									   D3D12CommandQueue *commandQueue,
									   D3D12DescriptorHeap *globalDescriptorHeap,
									   void *hwnd,
									   uint32_t framesInFlight) : descriptorHeap_(globalDescriptorHeap)
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	ImGuiIO &io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

	ImGui::Spectrum::StyleColorsSpectrum();
	// Allocate a dedicated descriptor slot for ImGui's font texture SRV.
	fontDescriptorHandle_ = globalDescriptorHeap->AllocateDescriptor();
	CPUDescriptorHandle cpuHandle = globalDescriptorHeap->GetCPUDescriptorHandle(fontDescriptorHandle_);
	GPUDescriptorHandle gpuHandle = globalDescriptorHeap->GetGPUDescriptorHandle(fontDescriptorHandle_);


	ImGui_ImplWin32_Init(static_cast<HWND>(hwnd));

	ImGui_ImplDX12_InitInfo initInfo = {};
	initInfo.Device = device->GetNativeDevice().Get();
	initInfo.CommandQueue = commandQueue->GetNativeCommandQueue().Get();
	initInfo.NumFramesInFlight = static_cast<int>(framesInFlight);
	initInfo.RTVFormat = rtvFormat;
	initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
	initInfo.SrvDescriptorHeap = globalDescriptorHeap->GetResourceDescriptorHeap();
	initInfo.LegacySingleSrvCpuDescriptor = cpuHandle;
	initInfo.LegacySingleSrvGpuDescriptor = gpuHandle;
	ImGui_ImplDX12_Init(&initInfo);
	fontBackendGpuHandle_ = initInfo.LegacySingleSrvGpuDescriptor.ptr;
	fontDrawGpuHandle_ = fontBackendGpuHandle_;

	ImGui::Spectrum::LoadFont();

	globalDescriptorHeap->MarkDirty();
}

D3D12ImguiRenderer::~D3D12ImguiRenderer()
{
	SetFontTextureHandle(fontDrawGpuHandle_, fontBackendGpuHandle_);
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
	if (descriptorHeap_ && fontDescriptorHandle_ != InvalidDescriptorHandle)
		descriptorHeap_->FreeResource(fontDescriptorHandle_);
}

void D3D12ImguiRenderer::SetFontTextureHandle(UINT64 from, UINT64 to)
{
	if (from == to)
		return;
	for (ImTextureData *texture: ImGui::GetPlatformIO().Textures)
	{
		if (texture->TexID == (ImTextureID) from)
			texture->SetTexID((ImTextureID) to);
	}
}

void D3D12ImguiRenderer::RenderUI(D3D12CommandList *commandList)
{
	ImGui::Render();

	SetFontTextureHandle(fontDrawGpuHandle_, fontBackendGpuHandle_);
	bool updated = false;
	for (ImTextureData *texture: ImGui::GetPlatformIO().Textures)
	{
		if (texture->Status != ImTextureStatus_OK)
		{
			ImGui_ImplDX12_UpdateTexture(texture);
			updated = true;
		}
	}
	if (updated)
	{
		descriptorHeap_->MarkDirty();
		descriptorHeap_->Flush();
	}
	fontDrawGpuHandle_ = descriptorHeap_->GetGPUDescriptorHandle(fontDescriptorHandle_).ptr;
	SetFontTextureHandle(fontBackendGpuHandle_, fontDrawGpuHandle_);

	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList->GetNativeCommandList().Get());
}
