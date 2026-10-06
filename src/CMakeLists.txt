add_subdirectory(D3D12MemoryAllocator)
add_subdirectory(imgui)

add_library(D3D12 STATIC
D3D12Device.cpp
D3D12CommandQueue.cpp
D3D12Swapchain.cpp
D3D12CommandList.cpp
D3D12Fence.cpp
D3D12GPUProfiler.cpp
D3D12Pipeline.cpp
D3D12RootSignatureBuilder.cpp
D3D12DescriptorHeap.cpp
D3D12ShaderTable.cpp
D3D12AccelerationStructure.cpp
SlangCompiler.cpp
D3D12ImguiRenderer.cpp
HelperClasses/RenderTargetHelper.cpp
)

target_precompile_headers(D3D12 PRIVATE pch.h)

target_link_libraries(D3D12 PUBLIC Microsoft::DirectX-Headers mimalloc-static PRIVATE d3d12 d3d11 dxgi dxguid fmtlog::fmtlog_static slang::slang D3D12MemoryAllocator imgui)
target_include_directories(D3D12 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/D3D12MemoryAllocator/include imgui)
target_compile_definitions(D3D12 PRIVATE D3D12_AGILITY_SDK_VERSION=${AGILITY_SDK_VERSION_NUMBER})

# Slang and DXC next to the executable, and the Agility SDK runtime in the D3D12\ folder that
# D3D12Device.cpp exports as D3D12SDKPath.
function(d3d12_deploy_runtime_dlls target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${SLANG_RUNTIME_DIR}" "$<TARGET_FILE_DIR:${target}>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${DXC_RUNTIME_DIR}/dxcompiler.dll"
            "${DXC_RUNTIME_DIR}/dxil.dll"
            "$<TARGET_FILE_DIR:${target}>"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/D3D12"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${AGILITY_SDK_RUNTIME_DIR}/D3D12Core.dll"
            "${AGILITY_SDK_RUNTIME_DIR}/d3d12SDKLayers.dll"
            "$<TARGET_FILE_DIR:${target}>/D3D12"
        COMMENT "Deploying Slang, DXC and D3D12 Agility SDK runtime DLLs next to ${target}")
endfunction()
