#ifndef RENDERER_GRAPHICS_PIPELINE_BUILDER_HPP
#define RENDERER_GRAPHICS_PIPELINE_BUILDER_HPP

// libs
#include <vulkan/vulkan.hpp>

// std
#include <filesystem>

namespace renderer
{
class VulkanGraphicsContext;

class GraphicsPipelineBuilder
{
public:
    GraphicsPipelineBuilder() = delete;
    explicit GraphicsPipelineBuilder(const VulkanGraphicsContext& context) noexcept;

    GraphicsPipelineBuilder(const GraphicsPipelineBuilder&) = delete;
    GraphicsPipelineBuilder& operator=(const GraphicsPipelineBuilder&) = delete;

    GraphicsPipelineBuilder(GraphicsPipelineBuilder&&) = delete;
    GraphicsPipelineBuilder& operator=(GraphicsPipelineBuilder&&) = delete;

    ~GraphicsPipelineBuilder() noexcept = default;

public:
    void setShaders(const std::filesystem::path& vertexShaderSourcePath,
                    const std::filesystem::path& fragmentShaderSourcePath);

    void setPipelineLayout(vk::PipelineLayout layout) noexcept;

    // TODO: add more supported color formats to the pipeline instead of only using the current swapchain format
    vk::UniquePipeline build();

private:
    const VulkanGraphicsContext& m_context;
    vk::UniqueShaderModule m_vertShaderModule;
    vk::UniqueShaderModule m_fragShaderModule;
    vk::PipelineLayout m_pipelineLayout;   // not owned
};

}   // namespace renderer

#endif
