cmake_minimum_required(VERSION 3.14)

include(FetchContent)

# Platform / architecture detection for the Slang release archive name.
if(WIN32)
    set(SLANG_PLATFORM "windows")
elseif(APPLE)
    set(SLANG_PLATFORM "macos")
else()
    set(SLANG_PLATFORM "linux")
endif()

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|ARM64|arm64)$")
    set(SLANG_ARCH "aarch64")
else()
    set(SLANG_ARCH "x86_64")
endif()

set(SLANG_VERSION "2026.16.1")
message(STATUS "Slang: ${SLANG_VERSION} for ${SLANG_PLATFORM}-${SLANG_ARCH}")

if(WIN32)
    set(SLANG_ARCHIVE_EXT "zip")
else()
    set(SLANG_ARCHIVE_EXT "tar.gz")
endif()

set(SLANG_URL
    "https://github.com/shader-slang/slang/releases/download/v${SLANG_VERSION}/slang-${SLANG_VERSION}-${SLANG_PLATFORM}-${SLANG_ARCH}.${SLANG_ARCHIVE_EXT}")

FetchContent_Declare(
    slang_sdk
    URL "${SLANG_URL}"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(slang_sdk)

# The release archive ships its own CMake package config with imported targets
# slang::slang (SHARED IMPORTED, include dirs + SLANG_DYNAMIC set) and slang::slangc.
find_package(slang CONFIG REQUIRED
    PATHS "${slang_sdk_SOURCE_DIR}/cmake"
    NO_DEFAULT_PATH)

# Directory holding slang.dll and its runtime companions (slang-glslang.dll, slang-llvm.dll,
# ...). Consumers that link the D3D12 static library must copy these next to their executable;
# d3d12_deploy_runtime_dlls() (defined in src/CMakeLists.txt) does that.
set(SLANG_RUNTIME_DIR "${slang_sdk_SOURCE_DIR}/bin" CACHE INTERNAL "Slang runtime DLL directory")

message(STATUS "Slang ${SLANG_VERSION} ready (runtime DLLs: ${SLANG_RUNTIME_DIR})")

# Offline shader compilation helper (optional; retargeted at slang::slangc).
function(add_slang_shader SHADER_FILE TARGET_FORMAT OUTPUT_FILE)
    if(TARGET_FORMAT STREQUAL "DXIL")
        set(ARGS -target dxil -profile sm_6_6)
    elseif(TARGET_FORMAT STREQUAL "SPIRV")
        set(ARGS -target spirv -profile glsl_450)
    else()
        message(FATAL_ERROR "Unsupported format: ${TARGET_FORMAT}")
    endif()

    add_custom_command(
        OUTPUT ${OUTPUT_FILE}
        COMMAND slang::slangc ${SHADER_FILE} ${ARGS} -o ${OUTPUT_FILE}
        DEPENDS ${SHADER_FILE}
        COMMENT "Compiling ${SHADER_FILE}"
    )
endfunction()
