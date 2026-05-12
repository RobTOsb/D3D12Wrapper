add_subdirectory(D3D12MemoryAllocator)
add_subdirectory(imgui)

add_library(D3D12 STATIC 
D3D12Device.cpp 
D3D12CommandQueue.cpp 
D3D12Swapchain.cpp 
D3D12CommandList.cpp
D3D12Fence.cpp
D3D12Pipeline.cpp
D3D12DescriptorHeap.cpp
DXCompiler.cpp
D3D12ImguiRenderer.cpp
HelperClasses/RenderTargetHelper.cpp
)

target_precompile_headers(D3D12 PRIVATE pch.h)

target_link_libraries(D3D12 PUBLIC Microsoft::DirectX-Headers PRIVATE d3d12 d3d11 dxgi dxguid fmtlog::fmtlog_static mimalloc-static dxcompiler D3D12MemoryAllocator imgui)
target_include_directories(D3D12 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/D3D12MemoryAllocator/include imgui)