#include "D3D12ImguiRenderer.h"

#include "D3D12Device.h"
#include "D3D12DescriptorHeap.h"
#include "D3D12CommandQueue.h"
#include "D3D12CommandList.h"

D3D12ImguiRenderer::D3D12ImguiRenderer(D3D12Device *device,
									DXGI_FORMAT rtvFormat,
										D3D12CommandQueue *commandQueue,
										D3D12DescriptorHeap *globalDescriptorHeap,
										void *hwnd,
										uint32_t frameCount) :
	descriptorHeap_(globalDescriptorHeap)
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
	initInfo.NumFramesInFlight = static_cast<int>(frameCount);
	initInfo.RTVFormat = rtvFormat;
	initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
	initInfo.SrvDescriptorHeap = globalDescriptorHeap->GetResourceDescriptorHeap();
	initInfo.LegacySingleSrvCpuDescriptor = cpuHandle;
	initInfo.LegacySingleSrvGpuDescriptor = gpuHandle;
	ImGui_ImplDX12_Init(&initInfo);

    ImGui::Spectrum::LoadFont();
}

D3D12ImguiRenderer::~D3D12ImguiRenderer()
{
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
	if (descriptorHeap_ && fontDescriptorHandle_ != InvalidDescriptorHandle)
		descriptorHeap_->FreeResource(fontDescriptorHandle_);
}

void D3D12ImguiRenderer::RenderUI(D3D12CommandList *commandList)
{
	ImGui::Render();
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList->GetNativeCommandList().Get());
}