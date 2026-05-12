#pragma once
#include "pch.h"

// Forward declarations
struct ShaderCompilationResult;

enum class ShaderVisibility : uint32_t
{
	VERTEX_SHADER = 0x1,
	HULL_SHADER = 0x2, // Tessellation control shader
	DOMAIN_SHADER = 0x4, // Tessellation evaluation shader
	GEOMETRY_SHADER = 0x8,
	PIXEL_SHADER = 0x10,
	COMPUTE_SHADER = 0x20,
	MESH_SHADER = 0x40, // Modern mesh shading pipeline
	AMPLIFICATION_SHADER = 0x80, // Task shader in Vulkan terms
	RAY_GENERATION_SHADER = 0x100,
	RAY_MISS_SHADER = 0x200,
	RAY_CLOSEST_HIT_SHADER = 0x400,
	RAY_ANY_HIT_SHADER = 0x800,
	RAY_INTERSECTION_SHADER = 0x1000,
	RAY_CALLABLE_SHADER = 0x2000,

	// Common combinations
	ALL_GRAPHICS = VERTEX_SHADER | HULL_SHADER | DOMAIN_SHADER | GEOMETRY_SHADER | PIXEL_SHADER | MESH_SHADER |
				   AMPLIFICATION_SHADER,
	ALL_RAYTRACING = RAY_GENERATION_SHADER | RAY_MISS_SHADER | RAY_CLOSEST_HIT_SHADER | RAY_ANY_HIT_SHADER |
					 RAY_INTERSECTION_SHADER | RAY_CALLABLE_SHADER,
	ALL = ALL_GRAPHICS | COMPUTE_SHADER | ALL_RAYTRACING
};

// Bitwise operators for ShaderVisibility
inline ShaderVisibility operator|(ShaderVisibility a, ShaderVisibility b)
{
	return static_cast<ShaderVisibility>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline ShaderVisibility operator&(ShaderVisibility a, ShaderVisibility b)
{
	return static_cast<ShaderVisibility>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline ShaderVisibility &operator|=(ShaderVisibility &a, ShaderVisibility b)
{
	return a = a | b;
}

inline ShaderVisibility &operator&=(ShaderVisibility &a, ShaderVisibility b)
{
	return a = a & b;
}

inline bool HasFlag(ShaderVisibility flags, ShaderVisibility flag)
{
	return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(flag)) != 0;
}

// Descriptor range types (what the descriptor points to)
enum class DescriptorRangeType : uint32_t
{
	CBV, // Constant Buffer View
	SRV_BUFFER, // Shader Resource View (buffers for reading)
	SRV_TEXTURE, // Shader Resource View (textures for reading)
	UAV_BUFFER, // Unordered Access View (buffers for read/write)
	UAV_TEXTURE, // Unordered Access View (textures for read/write)
	SAMPLER, // Sampler state
	COUNT
};

// Descriptor parameter types (how the descriptor is bound)
enum class DescriptorParameterType : uint32_t
{
	DescriptorTable, // Vulkan: descriptor set, D3D12: descriptor table
	Constants, // Vulkan: push constants, D3D12: root constants
	CBV, // Vulkan: inline uniform buffer, D3D12: root CBV
	SRV, // Vulkan: inline storage buffer, D3D12: root SRV
	UAV // Vulkan: inline storage buffer, D3D12: root UAV
};

// Push constants / root constants
struct PushConstants
{
	uint32_t num32BitValues; // D3D12 uses 32-bit values, Vulkan uses bytes (multiply by 4)
	uint32_t shaderRegister; // register(bX) in HLSL, binding in GLSL
	uint32_t registerSpace; // space in HLSL, set in Vulkan
	ShaderVisibility visibility;
};

// Descriptor range - defines a contiguous range of descriptors in a table
struct DescriptorRange
{
	DescriptorRangeType rangeType; // Type of descriptors in the range
	uint32_t numDescriptors; // Number of descriptors in the range
	uint32_t baseShaderRegister; // Starting register (e.g., t0, u0, s0, b0)
	uint32_t registerSpace; // HLSL space, Vulkan set
	uint32_t offsetInDescriptorsFromTableStart; // Offset from start of table
};

// Inline descriptor (root descriptor in D3D12)
struct InlineDescriptor
{
	uint32_t shaderRegister;
	uint32_t registerSpace;
	ShaderVisibility visibility;
};

// Descriptor table - contains one or more descriptor ranges
struct DescriptorTable
{
	std::vector<DescriptorRange> ranges;
	ShaderVisibility visibility;
};

// Sampler state parameters
enum class Filter
{
	POINT,
	LINEAR,
	ANISOTROPIC
};

enum class AddressMode
{
	WRAP,
	CLAMP,
	BORDER
};

// Static sampler (immutable sampler defined at pipeline creation)
struct StaticSampler
{
	ShaderVisibility visibility;

	Filter filter = Filter::LINEAR;

	AddressMode addressU = AddressMode::WRAP;
	AddressMode addressV = AddressMode::WRAP;
	AddressMode addressW = AddressMode::WRAP;
	float mipLODBias = 0.0f;
	uint32_t maxAnisotropy = 16;
	float minLOD = 0.0f;
	float maxLOD = 3.402823466e+38f; // FLT_MAX
};

// Sampler state - represents a sampler in the pipeline layout/root signature
struct SamplerState
{
	void *samplerHandle = nullptr;

	ShaderVisibility visibility;
	Filter filter = Filter::LINEAR;

	AddressMode addressU = AddressMode::WRAP;
	AddressMode addressV = AddressMode::WRAP;
	AddressMode addressW = AddressMode::WRAP;
	float mipLODBias = 0.0f;
	uint32_t maxAnisotropy = 16;
	float minLOD = 0.0f;
	float maxLOD = 3.402823466e+38f; // FLT_MAX
};

// Descriptor binding - represents one parameter in the pipeline layout/root signature
struct DescriptorBinding
{
	DescriptorParameterType type;

	// Only one of these is valid depending on type
	PushConstants constants{};
	InlineDescriptor descriptor{};
	DescriptorTable descriptorTable;

	// Helper constructors
	static DescriptorBinding InitAsConstants(uint32_t num32BitValues,
											 uint32_t shaderRegister,
											 uint32_t registerSpace = 0,
											 ShaderVisibility visibility = ShaderVisibility::ALL)
	{
		DescriptorBinding binding;
		binding.type = DescriptorParameterType::Constants;
		binding.constants = { num32BitValues, shaderRegister, registerSpace, visibility };
		return binding;
	}

	static DescriptorBinding InitAsConstantBufferView(uint32_t shaderRegister,
													  uint32_t registerSpace = 0,
													  ShaderVisibility visibility = ShaderVisibility::ALL)
	{
		DescriptorBinding binding;
		binding.type = DescriptorParameterType::CBV;
		binding.descriptor = { shaderRegister, registerSpace, visibility };
		return binding;
	}

	static DescriptorBinding InitAsShaderResourceView(uint32_t shaderRegister,
													  uint32_t registerSpace = 0,
													  ShaderVisibility visibility = ShaderVisibility::ALL)
	{
		DescriptorBinding binding;
		binding.type = DescriptorParameterType::SRV;
		binding.descriptor = { shaderRegister, registerSpace, visibility };
		return binding;
	}

	static DescriptorBinding InitAsUnorderedAccessView(uint32_t shaderRegister,
													   uint32_t registerSpace = 0,
													   ShaderVisibility visibility = ShaderVisibility::ALL)
	{
		DescriptorBinding binding;
		binding.type = DescriptorParameterType::UAV;
		binding.descriptor = { shaderRegister, registerSpace, visibility };
		return binding;
	}

	static DescriptorBinding InitAsDescriptorTable(const std::vector<DescriptorRange> &ranges,
												   ShaderVisibility visibility = ShaderVisibility::ALL)
	{
		DescriptorBinding binding;
		binding.type = DescriptorParameterType::DescriptorTable;
		binding.descriptorTable = { ranges, visibility };
		return binding;
	}
};

struct PipelineLayoutCreateInfo
{
	std::vector<DescriptorBinding> bindings;
	std::vector<StaticSampler> staticSamplers;

	void AddBinding(const DescriptorBinding &binding)
	{
		bindings.push_back(binding);
	}

	void AddStaticSampler(const StaticSampler &sampler)
	{
		staticSamplers.push_back(sampler);
	}
};

enum class PrimitiveTopology : uint32_t
{
	POINT_LIST,
	LINE_LIST,
	LINE_STRIP,
	TRIANGLE_LIST,
	TRIANGLE_STRIP,
	LINE_LIST_ADJ,
	LINE_STRIP_ADJ,
	TRIANGLE_LIST_ADJ,
	TRIANGLE_STRIP_ADJ,
	PATCH_LIST
};

enum class PipelineType : uint32_t
{
	GRAPHICS_VERTEX, // Traditional vertex + pixel shader pipeline
	GRAPHICS_MESH, // Modern mesh + amplification shader pipeline
	COMPUTE
};

struct InputElement
{
	const char* semanticName;
	uint32_t semanticIndex;
	DXGI_FORMAT format;
	uint32_t inputSlot;
	uint32_t alignedByteOffset;
	D3D12_INPUT_CLASSIFICATION inputSlotClass;
	uint32_t instanceDataStepRate;
};

struct RasterizerStateDesc
{
    D3D12_FILL_MODE fillMode = D3D12_FILL_MODE_SOLID;
    D3D12_CULL_MODE cullMode = D3D12_CULL_MODE_BACK;
    BOOL frontCounterClockwise = TRUE;
    INT depthBias = 0;
    float depthBiasClamp = 0.0f;
    float slopeScaledDepthBias = 0.0f;
    BOOL depthClipEnable = TRUE;
    BOOL multisampleEnable = FALSE;
    BOOL antialiasedLineEnable = FALSE;

    static RasterizerStateDesc Default()
    {
        return RasterizerStateDesc{};
    }
};

struct BlendStateDesc
{
    BOOL alphaToCoverageEnable = FALSE;
    BOOL independentBlendEnable = FALSE;
    D3D12_RENDER_TARGET_BLEND_DESC renderTargetBlend;
    std::vector<D3D12_RENDER_TARGET_BLEND_DESC> rtBlends;

    static BlendStateDesc OpaqueDefault()
    {
        BlendStateDesc desc;

        D3D12_RENDER_TARGET_BLEND_DESC rtBlend = {};
        rtBlend.BlendEnable = FALSE;
        rtBlend.LogicOpEnable = FALSE;
        rtBlend.SrcBlend = D3D12_BLEND_ONE;
        rtBlend.DestBlend = D3D12_BLEND_ZERO;
        rtBlend.BlendOp = D3D12_BLEND_OP_ADD;
        rtBlend.SrcBlendAlpha = D3D12_BLEND_ONE;
        rtBlend.DestBlendAlpha = D3D12_BLEND_ZERO;
        rtBlend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        rtBlend.LogicOp = D3D12_LOGIC_OP_NOOP;
        rtBlend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        desc.renderTargetBlend = rtBlend;
        return desc;
    }

	// static BlendStateDesc MaskDefault()
	// {
	// 	BlendStateDesc desc;

	// 	D3D12_RENDER_TARGET_BLEND_DESC rtBlend = {};
	// 	rtBlend.BlendEnable = FALSE;
	// 	rtBlend.LogicOpEnable = FALSE;
	// 	rtBlend.SrcBlend = D3D12_BLEND_ZERO;
	// 	rtBlend.DestBlend = D3D12_BLEND_ONE;
	// 	rtBlend.BlendOp = D3D12_BLEND_OP_ADD;
	// 	rtBlend.SrcBlendAlpha = D3D12_BLEND_ZERO;
	// 	rtBlend.DestBlendAlpha = D3D12_BLEND_ONE;
	// 	rtBlend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	// 	rtBlend.LogicOp = D3D12_LOGIC_OP_NOOP;
	// 	rtBlend.RenderTargetWriteMask = 0; // No color writes
	// 	desc.renderTargetBlend = rtBlend;
	// 	return desc;
	// }

	static BlendStateDesc AlphaBlend()
	{
		BlendStateDesc desc;
		D3D12_RENDER_TARGET_BLEND_DESC rtBlend = {};
		rtBlend.BlendEnable = TRUE;
		rtBlend.LogicOpEnable = FALSE;
		rtBlend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
		rtBlend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
		rtBlend.BlendOp = D3D12_BLEND_OP_ADD;
		rtBlend.SrcBlendAlpha = D3D12_BLEND_ONE;
		rtBlend.DestBlendAlpha = D3D12_BLEND_ZERO;
		rtBlend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
		rtBlend.LogicOp = D3D12_LOGIC_OP_NOOP;
		rtBlend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		desc.renderTargetBlend = rtBlend;
		return desc;
	}

	// WBOIT: two render targets.
	// RTV[0] (accum R16G16B16A16_FLOAT): additive ONE/ONE for both colour and alpha channels.
	// RTV[1] (reveal R16_FLOAT): product-of-(1-alpha): shader writes (1-alpha) to src.r,
	//   so use ZERO/SRC_COLOR so dst.r = dst.r * src.r = dst.r * (1-alpha).
	static BlendStateDesc WBOITBlend()
	{
		BlendStateDesc desc;
		desc.alphaToCoverageEnable  = FALSE;
		desc.independentBlendEnable = TRUE;

		// Accum target: additive accumulation of weighted colour+alpha.
		D3D12_RENDER_TARGET_BLEND_DESC accum = {};
		accum.BlendEnable             = TRUE;
		accum.LogicOpEnable           = FALSE;
		accum.SrcBlend                = D3D12_BLEND_ONE;
		accum.DestBlend               = D3D12_BLEND_ONE;
		accum.BlendOp                 = D3D12_BLEND_OP_ADD;
		accum.SrcBlendAlpha           = D3D12_BLEND_ONE;
		accum.DestBlendAlpha          = D3D12_BLEND_ONE;
		accum.BlendOpAlpha            = D3D12_BLEND_OP_ADD;
		accum.LogicOp                 = D3D12_LOGIC_OP_NOOP;
		accum.RenderTargetWriteMask   = D3D12_COLOR_WRITE_ENABLE_ALL;

		// Reveal target: product-of-(1-alpha) accumulated in the red channel.
		// Shader writes src.r = (1-alpha), src.a = 0.
		// dst.r = 0 * src.r + src.r * dst.r = (1-alpha) * dst.r  (SRC_COLOR reads src.r)
		D3D12_RENDER_TARGET_BLEND_DESC reveal = {};
		reveal.BlendEnable            = TRUE;
		reveal.LogicOpEnable          = FALSE;
		reveal.SrcBlend               = D3D12_BLEND_ZERO;
		reveal.DestBlend              = D3D12_BLEND_SRC_COLOR;
		reveal.BlendOp                = D3D12_BLEND_OP_ADD;
		reveal.SrcBlendAlpha          = D3D12_BLEND_ZERO;
		reveal.DestBlendAlpha         = D3D12_BLEND_SRC_ALPHA;
		reveal.BlendOpAlpha           = D3D12_BLEND_OP_ADD;
		reveal.LogicOp                = D3D12_LOGIC_OP_NOOP;
		reveal.RenderTargetWriteMask  = D3D12_COLOR_WRITE_ENABLE_RED;

		desc.rtBlends = { accum, reveal };
		return desc;
	}
};

struct StencilOpDesc
{
    D3D12_STENCIL_OP stencilFailOp = D3D12_STENCIL_OP_KEEP;
    D3D12_STENCIL_OP stencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    D3D12_STENCIL_OP stencilPassOp = D3D12_STENCIL_OP_KEEP;
    D3D12_COMPARISON_FUNC stencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
};

struct DepthStencilStateDesc
{
    BOOL depthEnable = TRUE;
    D3D12_DEPTH_WRITE_MASK depthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    D3D12_COMPARISON_FUNC depthFunc = D3D12_COMPARISON_FUNC_LESS;
    BOOL stencilEnable = FALSE;
    uint8_t stencilReadMask = 0xFF;
    uint8_t stencilWriteMask = 0xFF;
    StencilOpDesc frontFace;
    StencilOpDesc backFace;
    
    static DepthStencilStateDesc Default()
    {
        return DepthStencilStateDesc{};
    }
};

struct GraphicsPipelineCreateInfo
{
	PipelineType pipelineType = PipelineType::GRAPHICS_VERTEX;
	
	// Rasterizer state
    RasterizerStateDesc rasterizerState;
	
	// Depth stencil state
    DepthStencilStateDesc depthStencilState;
	// Blend state
    BlendStateDesc blendState;
	// Input layout (only for vertex shader pipeline)
	std::vector<InputElement> inputElements;
	// Render target formats
	std::vector<DXGI_FORMAT> rtvFormats;
	DXGI_FORMAT dsvFormat = DXGI_FORMAT_UNKNOWN;
	// Topology
	PrimitiveTopology primitiveTopology = PrimitiveTopology::TRIANGLE_LIST;
	
	// Sample desc
	uint32_t sampleCount = 1;
	uint32_t sampleQuality = 0;
};

class D3D12GraphicsPipeline
{
public:
	D3D12GraphicsPipeline(Microsoft::WRL::ComPtr<ID3D12Device> device) : device_(device) {};
	~D3D12GraphicsPipeline() = default;

	D3D12GraphicsPipeline &BuildRootSignature(const PipelineLayoutCreateInfo &createInfo);
	D3D12GraphicsPipeline &BuildRootSignatureFromShader(const ShaderCompilationResult &compileResult);
	void BuildGraphicsPipeline(const GraphicsPipelineCreateInfo &pipelineInfo,
							   const ShaderCompilationResult &compileResult);
	
	Microsoft::WRL::ComPtr<ID3D12RootSignature> GetNativeRootSignature() const { return rootSignature_; }
	Microsoft::WRL::ComPtr<ID3D12PipelineState> GetNativePipelineState() const { return pipelineState_; }

private:
    void BuildMeshShaderPipeline(const GraphicsPipelineCreateInfo& pipelineInfo,
                                 const ShaderCompilationResult& compileResult);
	
	Microsoft::WRL::ComPtr<ID3D12Device> device_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
};

class D3D12ComputePipeline
{
public:
	D3D12ComputePipeline(Microsoft::WRL::ComPtr<ID3D12Device> device) : device_(device) {};
	~D3D12ComputePipeline() = default;

	D3D12ComputePipeline &BuildRootSignature(const PipelineLayoutCreateInfo &createInfo);
	D3D12ComputePipeline &BuildRootSignatureFromShader(const ShaderCompilationResult &compileResult);
	void BuildComputePipeline(const ShaderCompilationResult &compileResult);
	
	Microsoft::WRL::ComPtr<ID3D12RootSignature> GetNativeRootSignature() const { return rootSignature_; }
	Microsoft::WRL::ComPtr<ID3D12PipelineState> GetNativePipelineState() const { return pipelineState_; }

private:
	Microsoft::WRL::ComPtr<ID3D12Device> device_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
};
