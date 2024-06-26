#include "Image.hpp"

namespace renderer
{

// Begin Allocated2DImage
Allocated2DImage::Allocated2DImage(VmaAllocator allocator,
                                   vk::Format format,
                                   const vk::Extent2D& extent,
                                   vk::ImageTiling tiling,
                                   vk::ImageUsageFlags usage,
                                   VmaAllocatorCreateFlags allocationFlags,
                                   VmaMemoryUsage memoryUsage)
    : m_allocator(allocator)
{
    vk::ImageCreateInfo imageCreateInfo {
        .sType = vk::StructureType::eImageCreateInfo,
        .pNext = nullptr,
        .flags = {},
        .imageType = vk::ImageType::e2D,
        .format = format,
        .extent = {.width = extent.width, .height = extent.height, .depth = 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .tiling = tiling,
        .usage = usage,
        .sharingMode = vk::SharingMode::eExclusive,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
        .initialLayout = vk::ImageLayout::eUndefined
    };

    VmaAllocationCreateInfo allocationCreateInfo {.flags = allocationFlags,
                                                  .usage = memoryUsage,
                                                  .requiredFlags = {},
                                                  .preferredFlags = {},
                                                  .memoryTypeBits = {},
                                                  .pool = nullptr,
                                                  .pUserData = nullptr,
                                                  .priority = 0.f};

    vk::detail::resultCheck(
        static_cast<vk::Result>(vmaCreateImage(m_allocator,
                                               reinterpret_cast<VkImageCreateInfo*>(&imageCreateInfo),
                                               &allocationCreateInfo,
                                               reinterpret_cast<VkImage*>(&m_image),
                                               &m_allocation,
                                               nullptr)),
        "vmaCreateImage");
}

Allocated2DImage::Allocated2DImage(Allocated2DImage&& rhs) noexcept
    : m_allocator(std::exchange(rhs.m_allocator, nullptr))
    , m_allocation(rhs.m_allocation)
    , m_image(rhs.m_image)
{}

Allocated2DImage& Allocated2DImage::operator=(Allocated2DImage&& rhs) noexcept
{
    if (this != &rhs) {
        std::swap(m_allocator, rhs.m_allocator);
        std::swap(m_allocation, rhs.m_allocation);
        std::swap(m_image, rhs.m_image);
    }
    return *this;
}

Allocated2DImage::~Allocated2DImage() noexcept
{
    if (m_allocator) {
        vmaDestroyImage(m_allocator, m_image, m_allocation);
    }
}
// End Allocated2DImage

// Begin AllocatedTexture
AllocatedTexture::AllocatedTexture(std::shared_ptr<const Allocated2DImage> image,
                                   vk::UniqueImageView imageView,
                                   vk::DescriptorSet descriptor)
    : m_image(std::move(image))
    , m_imageView(std::move(imageView))
    , m_descriptor(descriptor)
{}
// End AllocatedTexture

}   // namespace renderer
