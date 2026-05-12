#pragma once
#include "pch.h"


struct ShaderCompilationResult;
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

	D3D12ComputePipeline &BuildRootSignatureFromShader(const ShaderCompilationResult &compileResult);
	void BuildComputePipeline(const ShaderCompilationResult &compileResult);
	
	Microsoft::WRL::ComPtr<ID3D12RootSignature> GetNativeRootSignature() const { return rootSignature_; }
	Microsoft::WRL::ComPtr<ID3D12PipelineState> GetNativePipelineState() const { return pipelineState_; }

private:
	Microsoft::WRL::ComPtr<ID3D12Device> device_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
};
