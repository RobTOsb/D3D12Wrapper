cmake_minimum_required(VERSION 3.14)

include(FetchContent)

# WinPixEventRuntime: PIX markers on command lists, which PIX timing captures need to name GPU work.
set(PIX_EVENT_RUNTIME_VERSION "1.0.240308001")
message(STATUS "WinPixEventRuntime: ${PIX_EVENT_RUNTIME_VERSION}")

FetchContent_Declare(
    pix_event_runtime
    URL "https://www.nuget.org/api/v2/package/WinPixEventRuntime/${PIX_EVENT_RUNTIME_VERSION}"
    DOWNLOAD_NAME "WinPixEventRuntime.${PIX_EVENT_RUNTIME_VERSION}.zip"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(pix_event_runtime)

set(PIX_EVENT_RUNTIME_DIR "${pix_event_runtime_SOURCE_DIR}/bin/x64" CACHE INTERNAL "WinPixEventRuntime library and DLL directory")

add_library(WinPixEventRuntime SHARED IMPORTED GLOBAL)
set_target_properties(WinPixEventRuntime PROPERTIES
    IMPORTED_LOCATION "${PIX_EVENT_RUNTIME_DIR}/WinPixEventRuntime.dll"
    IMPORTED_IMPLIB "${PIX_EVENT_RUNTIME_DIR}/WinPixEventRuntime.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${pix_event_runtime_SOURCE_DIR}/Include/WinPixEventRuntime")
