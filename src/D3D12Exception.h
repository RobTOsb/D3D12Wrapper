#pragma once

#include "pch.h"

class D3D12Exception : public std::runtime_error
{
public:
    D3D12Exception(const char* message, HRESULT hr) : std::runtime_error(message), hr_(hr) {}

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
