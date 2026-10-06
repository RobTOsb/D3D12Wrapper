
cmake_minimum_required(VERSION 3.28)
                
project("D3D12Wrapper")

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

include(cmake/CPM.cmake)
include(cmake/download_slang.cmake)
include(cmake/download_agility_sdk.cmake)

cpmaddpackage(
    NAME DirectXTex
    GIT_REPOSITORY https://github.com/microsoft/DirectXTex.git
    GIT_TAG mar2026
    EXCLUDE_FROM_ALL YES
    OPTIONS
        "BUILD_TOOLS OFF"
        "BUILD_SAMPLE OFF"
        "BUILD_DX11 OFF"
        "DIRECTXTEX_INSTALL OFF"
)

cpmaddpackage("gh:RobTOsb/fmtlog@2.2.3")
cpmaddpackage(
    NAME mimalloc
    GITHUB_REPOSITORY Microsoft/mimalloc
    VERSION 3.1.4
    OPTIONS
        "MI_BUILD_TESTS OFF"
)
cpmaddpackage("gh:Microsoft/DirectX-Headers@1.619.1")

add_subdirectory("src")

if(PROJECT_IS_TOP_LEVEL)
    enable_testing()
    add_subdirectory("test")
endif()
