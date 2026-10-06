cmake_minimum_required(VERSION 3.14)

include(FetchContent)

set(AGILITY_SDK_VERSION "1.619.6")
string(REGEX MATCH "^1\\.([0-9]+)\\." _agility_match "${AGILITY_SDK_VERSION}")
set(AGILITY_SDK_VERSION_NUMBER "${CMAKE_MATCH_1}" CACHE INTERNAL "D3D12SDKVersion of the Agility SDK runtime")
message(STATUS "D3D12 Agility SDK: ${AGILITY_SDK_VERSION} (D3D12SDKVersion ${AGILITY_SDK_VERSION_NUMBER})")

FetchContent_Declare(
    agility_sdk
    URL "https://www.nuget.org/api/v2/package/Microsoft.Direct3D.D3D12/${AGILITY_SDK_VERSION}"
    DOWNLOAD_NAME "Microsoft.Direct3D.D3D12.${AGILITY_SDK_VERSION}.zip"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(agility_sdk)

set(AGILITY_SDK_RUNTIME_DIR "${agility_sdk_SOURCE_DIR}/build/native/bin/x64" CACHE INTERNAL "Agility SDK runtime DLL directory")
