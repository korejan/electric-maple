// Copyright 2024, Collabora, Ltd.
//
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Vulkan-CUDA image interop utils
 * @author Korcan Hussein <korcan.hussein@collabora.com>
 * @ingroup aux_util
 */
#include <cstdint>
#include <cstring>
#include "ems_vk_cuda_image.h"
#include "xrt/xrt_compiler.h"
#include <cuda_runtime.h>

#ifdef XRT_OS_WINDOWS
#include <versionhelpers.h>
#include "ems_win32_security_attributes.hpp"
#endif

#include "xrt/xrt_defines.h"
#include "vk/vk_helpers.h"

namespace {

VkResult
create_exported_vk_image(const struct ems_create_cuda_image_info *create_info,
                         struct vk_image *out_image) {

    if (create_info == nullptr || out_image == nullptr) {
        return VK_ERROR_DEVICE_LOST;
    }

    vk_bundle *vk = create_info->vk;
    if (vk == NULL) {
        return VK_ERROR_DEVICE_LOST;
    }

    constexpr const VkExternalMemoryImageCreateInfo vkExternalMemImageCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
        .pNext = nullptr,
#ifdef XRT_OS_WINDOWS
        .handleTypes =  VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT,
#else
        .handleTypes =  VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT_KHR,
#endif
    };
    const VkImageCreateInfo imageInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = &vkExternalMemImageCreateInfo,
        .flags = create_info->flags,
        .imageType = VK_IMAGE_TYPE_2D,
        .format    = create_info->format,
        .extent {
            .width  = create_info->size.width,
            .height = create_info->size.height,
            .depth  = 1,
        },
        .mipLevels     = 1,
        .arrayLayers   = 1,
        .samples       = VK_SAMPLE_COUNT_1_BIT,
        .tiling        = create_info->image_tiling,
        .usage         = create_info->usage,
        .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices   = nullptr,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    auto vk_ret = vk->vkCreateImage(vk->device, 
                                    &imageInfo, create_info->alloc_callbacks, 
                                    &out_image->handle);
    if (vk_ret != VK_SUCCESS || out_image->handle == VK_NULL_HANDLE) {
        return vk_ret;
    }

    VkMemoryRequirements memRequirements = {};
    vk->vkGetImageMemoryRequirements(vk->device, out_image->handle, &memRequirements);

#ifdef XRT_OS_WINDOWS
    const em::WindowsSecurityAttributes winSecurityAttributes{};
    const VkExportMemoryWin32HandleInfoKHR vulkanExportMemoryWin32HandleInfoKHR = {
        .sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_WIN32_HANDLE_INFO_KHR,
        .pNext = nullptr,
        .pAttributes = &winSecurityAttributes,
        .dwAccess = DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
        .name = (LPCWSTR)nullptr,
    };
#endif
    const VkExportMemoryAllocateInfoKHR vulkanExportMemoryAllocateInfoKHR = {
        .sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO_KHR,
#ifdef XRT_OS_WINDOWS
        .pNext = IsWindows8OrGreater()? &vulkanExportMemoryWin32HandleInfoKHR : nullptr,
        .handleTypes = IsWindows8OrGreater()
            ? VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT
            : VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_KMT_BIT,
#else
        .pNext = nullptr,
        .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT_KHR,
#endif
    };

    {
        VkMemoryRequirements vkMemoryRequirements = {};
        vk->vkGetImageMemoryRequirements(vk->device, out_image->handle, &vkMemoryRequirements);
        out_image->size = vkMemoryRequirements.size;
    }

    uint32_t memory_type_index = UINT32_MAX;
	const bool bret = vk_get_memory_type(vk,
                                         memRequirements.memoryTypeBits,
                                         create_info->memory_property_flags,
                                         &memory_type_index);
	if (!bret) {
        vkDestroyImage(vk->device, out_image->handle, create_info->alloc_callbacks);
        out_image->handle = VK_NULL_HANDLE;
		return VK_ERROR_OUT_OF_DEVICE_MEMORY;
	}

    const VkMemoryAllocateInfo memAlloc = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &vulkanExportMemoryAllocateInfoKHR,
        .allocationSize = memRequirements.size,
        .memoryTypeIndex = memory_type_index,
    };
    vk_ret = vk->vkAllocateMemory(vk->device, &memAlloc, create_info->alloc_callbacks, &out_image->memory);
    if (vk_ret != VK_SUCCESS) {
        vkDestroyImage(vk->device, out_image->handle, create_info->alloc_callbacks);
        out_image->handle = VK_NULL_HANDLE;
        out_image->memory = VK_NULL_HANDLE;
		return vk_ret;
    }

    vk_ret = vkBindImageMemory(vk->device, out_image->handle, out_image->memory, 0);
    if (vk_ret != VK_SUCCESS) {
        vkDestroyImage(vk->device, out_image->handle, create_info->alloc_callbacks);
        vkFreeMemory(vk->device, out_image->memory, create_info->alloc_callbacks);
        out_image->handle = VK_NULL_HANDLE;
        out_image->memory = VK_NULL_HANDLE;
		return vk_ret;
    }
    
    return VK_SUCCESS;
}

#ifdef XRT_OS_WINDOWS
using cuda_vk_image_handle = HANDLE;
#else
using cuda_vk_image_handle = std::int32_t;
#endif

inline cuda_vk_image_handle
get_vk_image_mem_handle(vk_bundle* vk, 
                        VkDeviceMemory textureImageMemory,
                        VkExternalMemoryHandleTypeFlagsKHR externalMemoryHandleType) {
#ifdef XRT_OS_WINDOWS
    const VkMemoryGetWin32HandleInfoKHR vkMemoryGetWin32HandleInfoKHR = {
        .sType  = VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR,
        .pNext  = nullptr,
        .memory = textureImageMemory,
        .handleType = (VkExternalMemoryHandleTypeFlagBitsKHR)externalMemoryHandleType,
    };
    HANDLE handle{};
    vk->vkGetMemoryWin32HandleKHR(vk->device, &vkMemoryGetWin32HandleInfoKHR, &handle);
    return handle;
#else
    if (externalMemoryHandleType != VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT_KHR)
        return -1;
    const VkMemoryGetFdInfoKHR vkMemoryGetFdInfoKHR = {
        .sType  = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR,
        .pNext  = nullptr,
        .memory = textureImageMemory,
        .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT_KHR
    };
    int fd = -1;
    vk->vkGetMemoryFdKHR(vk->device, &vkMemoryGetFdInfoKHR, &fd);
    return fd;
#endif
}

constexpr inline cudaChannelFormatDesc create_channel_desc(const VkFormat fmt) {
    switch (fmt) {
    case VK_FORMAT_R8_UINT:
    case VK_FORMAT_R8_UNORM:
        return cudaCreateChannelDesc<std::uint8_t>();

    case VK_FORMAT_R16_UINT:
    case VK_FORMAT_R16_UNORM:
    case VK_FORMAT_R10X6_UNORM_PACK16:
        return cudaCreateChannelDesc<std::uint16_t>();

    case VK_FORMAT_R8G8_UINT:
        return cudaCreateChannelDesc(8, 8, 0, 0, cudaChannelFormatKindUnsigned);
    case VK_FORMAT_R8G8_UNORM:// return cudaCreateChannelDesc<std::uint16_t>();
        return cudaCreateChannelDesc(8, 8, 0, 0, cudaChannelFormatKindUnsignedNormalized8X2);

    case VK_FORMAT_R16G16_UINT:
        return cudaCreateChannelDesc(16, 16, 0, 0, cudaChannelFormatKindUnsigned);
    case VK_FORMAT_R10X6G10X6_UNORM_2PACK16:
    case VK_FORMAT_R16G16_UNORM:
        return cudaCreateChannelDesc(16, 16, 0, 0, cudaChannelFormatKindUnsignedNormalized16X2);

    case VK_FORMAT_G8_B8R8_2PLANE_420_UNORM:
        return cudaCreateChannelDescNV12();

    case VK_FORMAT_R8G8B8_SINT:
        return cudaCreateChannelDesc(8, 8, 8, 0, cudaChannelFormatKindSigned);
    case VK_FORMAT_R8G8B8_UINT:
        return cudaCreateChannelDesc(8, 8, 8, 0, cudaChannelFormatKindUnsigned);
    case VK_FORMAT_R8G8B8_UNORM:
    case VK_FORMAT_R8G8B8_SRGB:
        return cudaCreateChannelDesc(8, 8, 8, 0, cudaChannelFormatKindUnsignedNormalized8X4);

    case VK_FORMAT_R8G8B8A8_SINT:
        return cudaCreateChannelDesc(8, 8, 8, 8, cudaChannelFormatKindSigned);
    case VK_FORMAT_R8G8B8A8_UINT:
        return cudaCreateChannelDesc(8, 8, 8, 8, cudaChannelFormatKindUnsigned);
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_R8G8B8A8_SRGB:
        return cudaCreateChannelDesc(8, 8, 8, 8, cudaChannelFormatKindUnsignedNormalized8X4);
    
    default: return cudaCreateChannelDesc(0, 0, 0, 0, cudaChannelFormatKindNone);
    }
}

}

VkResult
ems_create_cuda_vk_image(const struct ems_create_cuda_image_info *create_info,
                         struct vk_cuda_image *out_image) {

    VkResult vk_ret = create_exported_vk_image(create_info, &out_image->base);
    if (vk_ret != VK_SUCCESS) {
        return vk_ret;
    }

    vk_bundle *vk = create_info->vk;

    VkDeviceMemory image_memory = out_image->base.memory;
    const std::uint32_t total_image_mem_size = out_image->base.size;
    auto& external_memory = out_image->cuda.external_memory;

    const cudaExternalMemoryHandleDesc cudaExtMemHandleDesc = {
#ifdef XRT_OS_WINDOWS
        .type = IsWindows8OrGreater() ? cudaExternalMemoryHandleTypeOpaqueWin32 : cudaExternalMemoryHandleTypeOpaqueWin32Kmt,
        .handle{.win32{.handle = get_vk_image_mem_handle(vk, image_memory, IsWindows8OrGreater()
                                    ? VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT
                                    : VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_KMT_BIT)}},
#else
        .type = cudaExternalMemoryHandleTypeOpaqueFd,
        .handle{ .fd = get_vk_image_mem_handle(vk, image_memory, VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT_KHR) },
#endif
        .size = total_image_mem_size,
        .flags = 0,
    };
    if (cudaImportExternalMemory(&external_memory, &cudaExtMemHandleDesc) != cudaSuccess) {
        vkDestroyImage(vk->device, out_image->base.handle, create_info->alloc_callbacks);
        out_image->base.handle = VK_NULL_HANDLE;
        return VK_ERROR_DEVICE_LOST;
    }

    const cudaExternalMemoryMipmappedArrayDesc cuExtmemMipDesc = {
        .offset = 0,
        .formatDesc = create_channel_desc(create_info->format),
        .extent     = make_cudaExtent(create_info->size.width, create_info->size.height, 0),                
        .flags      = cudaArrayColorAttachment,
        .numLevels  = 1
    };
    cudaMipmappedArray_t cuMipArray{};
    /*CheckCudaErrors*/
    if (cudaExternalMemoryGetMappedMipmappedArray(&cuMipArray, external_memory, &cuExtmemMipDesc) != cudaSuccess) {
        vkDestroyImage(vk->device, out_image->base.handle, create_info->alloc_callbacks);
        out_image->base.handle = VK_NULL_HANDLE;
        return VK_ERROR_DEVICE_LOST;
    }

    if (cudaGetMipmappedArrayLevel(&out_image->cuda.array, cuMipArray, 0) != cudaSuccess) {
        vkDestroyImage(vk->device, out_image->base.handle, create_info->alloc_callbacks);
        out_image->base.handle = VK_NULL_HANDLE;
        return VK_ERROR_DEVICE_LOST;
    }

    out_image->base.use_dedicated_allocation = false;

    return VK_SUCCESS;
}

bool
ems_find_matching_cuda_device(const struct xrt_uuid *vk_device_uuid, struct ems_vk_cuda_device *vk_cuda_device) {
    if (vk_device_uuid == nullptr || vk_cuda_device == nullptr) {
        return false;
    }

    int device_count = 0;
    if (cudaGetDeviceCount(&device_count) != cudaSuccess || device_count == 0) {
        return false;
    }

    //int devices_prohibited = 0;
    // Find the GPU which is selected by Vulkan
    for (int current_device = 0; current_device < device_count; ++current_device) {
        cudaDeviceProp device_prop = {};
        cudaGetDeviceProperties(&device_prop, current_device);
        if (device_prop.computeMode == cudaComputeModeProhibited) {
            //++devices_prohibited;
            continue;
        }

        // Compare the cuda device UUID with vulkan UUID
        const int result = std::memcmp((void*)&device_prop.uuid, vk_device_uuid->data, XRT_UUID_SIZE);
        if (result != 0) {
            continue;
        }

        cudaSetDevice(current_device);
        cudaGetDeviceProperties(&device_prop, current_device);
        // Log::Write(Log::Level::Info, Fmt("GPU Device %d: \"%s\" with compute capability %d.%d\n\n",
        // current_device, deviceProp.name, deviceProp.major, deviceProp.minor));
        
        vk_cuda_device->device_id = current_device;
        vk_cuda_device->node_mask = device_prop.luidDeviceNodeMask;
        
        return true;
    }
    return false;
}
