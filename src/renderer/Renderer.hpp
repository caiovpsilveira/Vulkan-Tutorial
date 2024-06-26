#ifndef RENDERER_RENDERER_HPP
#define RENDERER_RENDERER_HPP

#include "Image.hpp"
#include "Types.hpp"
#include "VulkanGraphicsContext.hpp"

// std
#include <filesystem>

struct SDL_Window;

namespace renderer
{

class Renderer
{
public:
    Renderer() noexcept = default;
    explicit Renderer(SDL_Window* window);

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    Renderer(Renderer&&) noexcept = default;
    Renderer& operator=(Renderer&&) noexcept = default;

    ~Renderer() noexcept = default;

public:
    void drawFrame();

private:
    void initTransferCommandData();
    void initFrameCommandData();

    void createGlobalDescriptorPool();
    void allocateFrameUboBuffers();
    vk::UniqueDescriptorSetLayout createGlobalDescriptorSets();

    void createTextureSampler();
    void createTextureDescriptorPool();
    vk::UniqueDescriptorSetLayout createTextureSetLayout();

    void createGraphicsPipeline(const std::vector<vk::DescriptorSetLayout>& descriptorSetLayouts);

    vk::UniqueCommandBuffer beginSingleTimeTransferCommand() const;
    void endSingleTimeTransferCommand(vk::UniqueCommandBuffer&& commandBuffer) const;

    static void transitionImageLayout(vk::CommandBuffer commandBuffer,
                                      vk::Image image,
                                      vk::Format format,
                                      vk::ImageLayout oldLayout,
                                      vk::ImageLayout newLayout);

    // TODO: move when being relative to a camera
    void updateUbo(vk::CommandBuffer command, vk::Buffer ubo, const vk::Extent2D& swapchainExtent) const;

    // TODO: create a render object? Somehow pass these to draw frame to be called extenally
    Allocated2DImage loadImage(const std::filesystem::path& path) const;
    AllocatedTexture createTexture(std::shared_ptr<const Allocated2DImage> image, vk::Format format) const;
    Mesh createMesh() const;

private:
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

    VulkanGraphicsContext m_vkContext;
    TransferCommandData m_transferCommandData;
    size_t m_frameCount = 0;
    FrameData m_frameData[MAX_FRAMES_IN_FLIGHT];

    vk::UniqueDescriptorPool m_globalDescriptorPool;

    vk::UniqueSampler m_textureSampler;
    vk::UniqueDescriptorPool m_textureDescriptorPool;
    vk::UniqueDescriptorSetLayout m_textureSetLayout;

    vk::UniquePipelineLayout m_graphicsPipelineLayout;
    vk::UniquePipeline m_graphicsPipeline;

    // TODO: remove
    Mesh m_testMesh;
    AllocatedTexture m_testTexture;
};

}   // namespace renderer

#endif

