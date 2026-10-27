#pragma once

#include "pch.h"

// Logs why the device was removed and what DRED recorded (where each unfinished command list stopped, the page
// fault and the allocations at its address), once, if it was. In D3D12Device.cpp.
void ReportDeviceRemoval();

class D3D12Exception : public std::runtime_error
{
public:
    // A call that fails once the device is removed comes here first, so the removal is reported where it is noticed.
    D3D12Exception(const char* message, HRESULT hr) : std::runtime_error(message), hr_(hr) { ReportDeviceRemoval(); }

    const char* what() const noexcept override
    {
        // Get error string from HRESULT
        static char errorString[512];
        FormatMessageA(
            FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr,
            hr_,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            errorString,
            sizeof(errorString),
            nullptr);
        return errorString;
    }

    HRESULT GetErrorCode() const noexcept
    {
        return hr_;
    }

private:
    HRESULT hr_;
};
