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
D3D12Aftermath.cpp
D3D12ImguiRenderer.cpp
HelperClasses/RenderTargetHelper.cpp
)

target_precompile_headers(D3D12 PRIVATE pch.h)

target_link_libraries(D3D12 PUBLIC Microsoft::DirectX-Headers mimalloc-static PRIVATE d3d12 d3d11 dxgi dxguid fmtlog::fmtlog_static slang::slang D3D12MemoryAllocator imgui WinPixEventRuntime)
target_include_directories(D3D12 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/D3D12MemoryAllocator/include imgui)
# windows.h comes in through the public headers, so consumers need its min/max macros kept away too.
target_compile_definitions(D3D12 PUBLIC NOMINMAX PRIVATE D3D12_AGILITY_SDK_VERSION=${AGILITY_SDK_VERSION_NUMBER})
# PIX markers in every configuration, not only debug and profile builds.
target_compile_definitions(D3D12 PRIVATE USE_PIX)

# NVIDIA Nsight Aftermath GPU crash dumps (D3D12Aftermath.h). The SDK ships with Nsight Graphics; the newest
# installed one is used unless AFTERMATH_SDK_DIR names another.
option(D3D12_AFTERMATH "Write Nsight Aftermath GPU crash dumps when the SDK is found" ON)
if(D3D12_AFTERMATH)
    if(NOT AFTERMATH_SDK_DIR)
        file(GLOB _aftermath_sdks "$ENV{ProgramFiles}/NVIDIA Corporation/Nsight Graphics */SDKs/NsightAftermathSDK/*")
        if(_aftermath_sdks)
            list(SORT _aftermath_sdks COMPARE NATURAL ORDER DESCENDING)
            list(GET _aftermath_sdks 0 _aftermath_sdk)
            set(AFTERMATH_SDK_DIR "${_aftermath_sdk}" CACHE PATH "Nsight Aftermath SDK folder, with include/ and lib/x64/")
        endif()
    endif()
    if(AFTERMATH_SDK_DIR AND EXISTS "${AFTERMATH_SDK_DIR}/include/GFSDK_Aftermath.h")
        message(STATUS "Nsight Aftermath SDK: ${AFTERMATH_SDK_DIR}")
        target_include_directories(D3D12 PRIVATE "${AFTERMATH_SDK_DIR}/include")
        target_link_libraries(D3D12 PRIVATE "${AFTERMATH_SDK_DIR}/lib/x64/GFSDK_Aftermath_Lib.x64.lib")
        target_compile_definitions(D3D12 PRIVATE D3D12_AFTERMATH)
        set(AFTERMATH_RUNTIME_DLL "${AFTERMATH_SDK_DIR}/lib/x64/GFSDK_Aftermath_Lib.x64.dll" CACHE INTERNAL "")
    else()
        message(STATUS "Nsight Aftermath SDK not found (set AFTERMATH_SDK_DIR); building without GPU crash dumps")
        set(AFTERMATH_RUNTIME_DLL "" CACHE INTERNAL "")
    endif()
else()
    set(AFTERMATH_RUNTIME_DLL "" CACHE INTERNAL "")
endif()

# Copies only the runtime DLLs: Slang, DXC and WinPixEventRuntime next to the executable, the Agility SDK in D3D12\.
# Slang to DXIL needs just slang.dll and slang-compiler.dll, not the SDK's tools or other targets.
set(SLANG_RUNTIME_DLLS
    "${SLANG_RUNTIME_DIR}/slang.dll"
    "${SLANG_RUNTIME_DIR}/slang-compiler.dll"
    CACHE INTERNAL "Slang DLLs deployed next to executables")

function(d3d12_deploy_runtime_dlls target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SLANG_RUNTIME_DLLS} "$<TARGET_FILE_DIR:${target}>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${DXC_RUNTIME_DIR}/dxcompiler.dll"
            "${DXC_RUNTIME_DIR}/dxil.dll"
            "${PIX_EVENT_RUNTIME_DIR}/WinPixEventRuntime.dll"
            "$<TARGET_FILE_DIR:${target}>"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/D3D12"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${AGILITY_SDK_RUNTIME_DIR}/D3D12Core.dll"
            "${AGILITY_SDK_RUNTIME_DIR}/d3d12SDKLayers.dll"
            "$<TARGET_FILE_DIR:${target}>/D3D12"
        COMMENT "Deploying Slang, DXC, WinPixEventRuntime and D3D12 Agility SDK runtime DLLs next to ${target}")
    if(AFTERMATH_RUNTIME_DLL)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${AFTERMATH_RUNTIME_DLL}" "$<TARGET_FILE_DIR:${target}>"
            COMMENT "Deploying the Nsight Aftermath DLL next to ${target}")
    endif()
endfunction()
