/*
 * you guys wanna be petty? ok, i dont need your stupid shit, rsjfw supremacy!!!
 */

#include "vk_layer.h"
#include <mutex>
#include <map>
#include <string.h>

#undef VK_LAYER_EXPORT
#if defined(WIN32)
#define VK_LAYER_EXPORT extern "C" __declspec(dllexport)
#else
#define VK_LAYER_EXPORT extern "C"
#endif

namespace rsjfw {

    std::mutex g_lock;
    std::map<void*, VkLayerInstanceDispatchTable> g_instanceDispatch;
    std::map<void*, VkLayerDispatchTable> g_deviceDispatch;
    bool g_triggerSwapchainRecreation = false;

    template<typename T>
    void* getKey(T object) {
        return *(void**)object;
    }

    VkLayerInstanceDispatchTable* getInstanceTable(VkInstance inst) {
        std::lock_guard<std::mutex> lock(g_lock);
        auto it = g_instanceDispatch.find(getKey(inst));
        return (it != g_instanceDispatch.end()) ? &it->second : nullptr;
    }

    VkLayerDispatchTable* getDeviceTable(VkDevice dev) {
        std::lock_guard<std::mutex> lock(g_lock);
        auto it = g_deviceDispatch.find(getKey(dev));
        return (it != g_deviceDispatch.end()) ? &it->second : nullptr;
    }
}

using namespace rsjfw;

VK_LAYER_EXPORT VkResult VKAPI_CALL RsjfwLayer_GetPhysicalDeviceSurfaceCapabilitiesKHR(
    VkPhysicalDevice physicalDevice, 
    VkSurfaceKHR surface, 
    VkSurfaceCapabilitiesKHR* pSurfaceCapabilities) 
{
    VkLayerInstanceDispatchTable* pTable = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        if (!g_instanceDispatch.empty()) pTable = &g_instanceDispatch.begin()->second;
    }

    if (pTable && pTable->GetPhysicalDeviceSurfaceCapabilitiesKHR) {
        VkResult res = pTable->GetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, pSurfaceCapabilities);
        
        if (g_triggerSwapchainRecreation) {
            g_triggerSwapchainRecreation = false;
            return VK_ERROR_SURFACE_LOST_KHR;
        }
        return res;
    }
    return VK_ERROR_INITIALIZATION_FAILED;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL RsjfwLayer_AcquireNextImageKHR(
    VkDevice device, 
    VkSwapchainKHR swapchain, 
    uint64_t timeout, 
    VkSemaphore semaphore, 
    VkFence fence, 
    uint32_t* pImageIndex) 
{
    auto* table = getDeviceTable(device);
    if (!table || !table->AcquireNextImageKHR) return VK_ERROR_INITIALIZATION_FAILED;

    VkResult res = table->AcquireNextImageKHR(device, swapchain, timeout, semaphore, fence, pImageIndex);

    if (res == VK_SUBOPTIMAL_KHR || res == VK_ERROR_OUT_OF_DATE_KHR) {
        g_triggerSwapchainRecreation = true;
        return VK_SUCCESS; 
    }

    return res;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL RsjfwLayer_CreateInstance(
    const VkInstanceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkInstance* pInstance)
{
    VkLayerInstanceCreateInfo* layerCreateInfo = (VkLayerInstanceCreateInfo*)pCreateInfo->pNext;
    while (layerCreateInfo && (layerCreateInfo->sType != VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO || layerCreateInfo->function != VK_LAYER_LINK_INFO)) {
        layerCreateInfo = (VkLayerInstanceCreateInfo*)layerCreateInfo->pNext;
    }

    if (!layerCreateInfo) return VK_ERROR_INITIALIZATION_FAILED;

    PFN_vkGetInstanceProcAddr gpa = layerCreateInfo->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    layerCreateInfo->u.pLayerInfo = layerCreateInfo->u.pLayerInfo->pNext;

    PFN_vkCreateInstance createFunc = (PFN_vkCreateInstance)gpa(VK_NULL_HANDLE, "vkCreateInstance");
    VkResult ret = createFunc(pCreateInfo, pAllocator, pInstance);

    if (ret == VK_SUCCESS) {
        VkLayerInstanceDispatchTable table;
        table.GetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)gpa(*pInstance, "vkGetInstanceProcAddr");
        table.GetPhysicalDeviceSurfaceCapabilitiesKHR = (PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR)gpa(*pInstance, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
        
        std::lock_guard<std::mutex> lock(g_lock);
        g_instanceDispatch[getKey(*pInstance)] = table;
    }
    return ret;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL RsjfwLayer_CreateDevice(
    VkPhysicalDevice physicalDevice,
    const VkDeviceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDevice* pDevice)
{
    VkLayerDeviceCreateInfo* layerCreateInfo = (VkLayerDeviceCreateInfo*)pCreateInfo->pNext;
    while (layerCreateInfo && (layerCreateInfo->sType != VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO || layerCreateInfo->function != VK_LAYER_LINK_INFO)) {
        layerCreateInfo = (VkLayerDeviceCreateInfo*)layerCreateInfo->pNext;
    }

    if (!layerCreateInfo) return VK_ERROR_INITIALIZATION_FAILED;

    PFN_vkGetInstanceProcAddr gipa = layerCreateInfo->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr gdpa = layerCreateInfo->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    layerCreateInfo->u.pLayerInfo = layerCreateInfo->u.pLayerInfo->pNext;

    PFN_vkCreateDevice createFunc = (PFN_vkCreateDevice)gipa(VK_NULL_HANDLE, "vkCreateDevice");
    VkResult ret = createFunc(physicalDevice, pCreateInfo, pAllocator, pDevice);

    if (ret == VK_SUCCESS) {
        VkLayerDispatchTable table;
        table.GetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)gdpa(*pDevice, "vkGetDeviceProcAddr");
        table.AcquireNextImageKHR = (PFN_vkAcquireNextImageKHR)gdpa(*pDevice, "vkAcquireNextImageKHR");
        
        std::lock_guard<std::mutex> lock(g_lock);
        g_deviceDispatch[getKey(*pDevice)] = table;
    }
    return ret;
}

VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL RsjfwLayer_GetDeviceProcAddr(VkDevice device, const char* pName) {
    if (!strcmp(pName, "vkGetDeviceProcAddr")) return (PFN_vkVoidFunction)RsjfwLayer_GetDeviceProcAddr;
    if (!strcmp(pName, "vkCreateDevice")) return (PFN_vkVoidFunction)RsjfwLayer_CreateDevice;
    if (!strcmp(pName, "vkAcquireNextImageKHR")) return (PFN_vkVoidFunction)RsjfwLayer_AcquireNextImageKHR;
    
    auto* table = getDeviceTable(device);
    return (table && table->GetDeviceProcAddr) ? table->GetDeviceProcAddr(device, pName) : nullptr;
}

VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL RsjfwLayer_GetInstanceProcAddr(VkInstance instance, const char* pName) {
    if (!strcmp(pName, "vkGetInstanceProcAddr")) return (PFN_vkVoidFunction)RsjfwLayer_GetInstanceProcAddr;
    if (!strcmp(pName, "vkCreateInstance")) return (PFN_vkVoidFunction)RsjfwLayer_CreateInstance;
    if (!strcmp(pName, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR")) return (PFN_vkVoidFunction)RsjfwLayer_GetPhysicalDeviceSurfaceCapabilitiesKHR;
    
    auto* table = getInstanceTable(instance);
    return (table && table->GetInstanceProcAddr) ? table->GetInstanceProcAddr(instance, pName) : nullptr;
}
