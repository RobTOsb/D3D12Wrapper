
cmake_minimum_required(VERSION 3.28)
                
project("D3D12Wrapper")

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

include(cmake/CPM.cmake)

cpmaddpackage(
    NAME DirectXTex
    GIT_REPOSITORY https://github.com/microsoft/DirectXTex.git
    GIT_TAG mar2026
    EXCLUDE_FROM_ALL YES
    OPTIONS
        "BUILD_TOOLS OFF"
        "BUILD_SAMPLE OFF"
        "DIRECTXTEX_INSTALL OFF"
)

cpmaddpackage("gh:RobTOsb/fmtlog@2.2.3")
cpmaddpackage("gh:Microsoft/mimalloc@3.1.4")
cpmaddpackage("gh:Microsoft/DirectX-Headers@1.619.1")

if(${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang" OR ${CMAKE_CXX_COMPILER_ID} STREQUAL "GNU")
    add_custom_target(copy_compile_commands ALL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_BINARY_DIR}/compile_commands.json
            ${CMAKE_SOURCE_DIR}/build/compile_commands.json
        DEPENDS ${CMAKE_BINARY_DIR}/compile_commands.json
    )
endif()

add_subdirectory("src")
