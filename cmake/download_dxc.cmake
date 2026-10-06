cmake_minimum_required(VERSION 3.14)

include(FetchContent)

# Slang compiles DXIL through whichever dxcompiler.dll LoadLibrary finds first, and the Windows SDK
# one is too old for Shader Model 6.9. d3d12_deploy_runtime_dlls() puts this one next to the exe.
set(DXC_VERSION "1.9.2609")

FetchContent_Declare(
    dxc
    URL "https://github.com/microsoft/DirectXShaderCompiler/releases/download/v${DXC_VERSION}/dxc_2026_09_29.zip"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(dxc)

set(DXC_RUNTIME_DIR "${dxc_SOURCE_DIR}/bin/x64" CACHE INTERNAL "DXC runtime DLL directory")
