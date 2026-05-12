#pragma once

#include "pch.h"

class D3D12Resource
{
public:
	D3D12Resource() = default;


	D3D12Resource(Microsoft::WRL::ComPtr<ID3D12Resource> resource) : resource_(resource)
	{
	}

	D3D12Resource(Microsoft::WRL::ComPtr<D3D12MA::Allocation> allocation) : allocation_(allocation)
	{
		resource_ = allocation_->GetResource();
	}

	virtual ~D3D12Resource() = default;

	ID3D12Resource *GetResource()
	{
		return resource_.Get();
	}

	D3D12MA::Allocation *GetAllocation()
	{
		return allocation_.Get();
	}

	void SetResource(Microsoft::WRL::ComPtr<ID3D12Resource> resource)
	{
		resource_ = resource;
	}

	void SetName(const wchar_t *name)
	{
		if (resource_)
			resource_->SetName(name);
		if (allocation_)
			allocation_->SetName(name);
	}

	void SetName(const std::string &name)
	{
		if (name.empty())
			return;
		const int size = MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, nullptr, 0);
		std::wstring wname(size, 0);
		MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, wname.data(), size);
		SetName(wname.c_str());
	}

protected:
	Microsoft::WRL::ComPtr<D3D12MA::Allocation> allocation_;
	Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
};
