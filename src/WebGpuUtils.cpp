#include "WebGpuUtils.h"
#include <cassert>
#include <iostream>
#include <vector>

WGPUAdapter Utils::requestAdapterSync(WGPUInstance instance, const WGPURequestAdapterOptions* options)
{
    // A simple structure holding the local information shared with the onAdapterRequestEnded callback.
    struct UserData
    {
        WGPUAdapter adapter = nullptr;
        bool requestEnded   = false;
    };
    UserData userData;

    // Callback called by wgpuInstanceRequestAdapter when the request returns
    auto onAdapterRequestEnded =
        [](WGPURequestAdapterStatus status, WGPUAdapter adapter, const char* message, void* pUserData)
    {
        UserData& userData = *reinterpret_cast<UserData*>(pUserData);
        if (status == WGPURequestAdapterStatus_Success)
        {
            userData.adapter = adapter;
        }
        else
        {
            std::cout << "Could not get WebGPU adapter: " << message << std::endl;
        }
        userData.requestEnded = true;
    };

    // Call to the WebGPU request adapter procedure
    wgpuInstanceRequestAdapter(instance, options, onAdapterRequestEnded, (void*)&userData);

    assert(userData.requestEnded);

    return userData.adapter;
}

WGPUDevice Utils::requestDeviceSync(WGPUAdapter adapter, const WGPUDeviceDescriptor* descriptor)
{
    struct UserData
    {
        WGPUDevice device = nullptr;
        bool requestEnded = false;
    };
    UserData userData;

    auto onDeviceRequestEnded =
        [](WGPURequestDeviceStatus status, WGPUDevice device, const char* message, void* pUserData)
    {
        UserData& userData = *reinterpret_cast<UserData*>(pUserData);
        if (status == WGPURequestDeviceStatus_Success)
        {
            userData.device = device;
        }
        else
        {
            std::cout << "Could not get WebGPU device: " << message << std::endl;
        }
        userData.requestEnded = true;
    };

    wgpuAdapterRequestDevice(adapter, descriptor, onDeviceRequestEnded, (void*)&userData);

    assert(userData.requestEnded);

    return userData.device;
}

void Utils::inspectAdapter(WGPUAdapter adapter)
{
    WGPUSupportedLimits supportedLimits = {};
    supportedLimits.nextInChain         = nullptr;

    bool success = wgpuAdapterGetLimits(adapter, &supportedLimits) == WGPUStatus_Success;

    if (success)
    {
        std::cout << "Adapter limits:" << std::endl;
        std::cout << " - maxTextureDimension1D: " << supportedLimits.limits.maxTextureDimension1D << std::endl;
        std::cout << " - maxTextureDimension2D: " << supportedLimits.limits.maxTextureDimension2D << std::endl;
        std::cout << " - maxTextureDimension3D: " << supportedLimits.limits.maxTextureDimension3D << std::endl;
        std::cout << " - maxTextureArrayLayers: " << supportedLimits.limits.maxTextureArrayLayers << std::endl;
    }

    std::vector<WGPUFeatureName> features;
    size_t featureCount = wgpuAdapterEnumerateFeatures(adapter, nullptr);
    features.resize(featureCount);
    wgpuAdapterEnumerateFeatures(adapter, features.data());

    std::cout << "Adapter features:" << std::endl;
    std::cout << std::hex;
    for (auto f : features)
    {
        std::cout << " - 0x" << f << std::endl;
    }
    std::cout << std::dec;

    WGPUAdapterProperties properties = {};
    properties.nextInChain           = nullptr;
    wgpuAdapterGetProperties(adapter, &properties);

    std::cout << "Adapter properties:" << std::endl;
    std::cout << " - vendorID: " << properties.vendorID << std::endl;
    if (properties.vendorName)
    {
        std::cout << " - vendorName: " << properties.vendorName << std::endl;
    }
    if (properties.architecture)
    {
        std::cout << " - architecture: " << properties.architecture << std::endl;
    }
    std::cout << " - deviceID: " << properties.deviceID << std::endl;
    if (properties.name)
    {
        std::cout << " - name: " << properties.name << std::endl;
    }
    if (properties.driverDescription)
    {
        std::cout << " - driverDescription: " << properties.driverDescription << std::endl;
    }
    std::cout << std::hex;
    std::cout << " - adapterType: 0x" << properties.adapterType << std::endl;
    std::cout << " - backendType: 0x" << properties.backendType << std::endl;
    std::cout << std::dec;
}

// void Utils::inspectDevice(WGPUDevice device) {}
