#include <iostream>
#include "WebGpuUtils.h"

int main()
{
    // create descriptor
    WGPUInstanceDescriptor desc = {};
    desc.nextInChain            = nullptr;

    // create instance
    WGPUInstance instance = wgpuCreateInstance(&desc);

    // check whether there is actually an insatnce created
    if (!instance)
    {
        std::cerr << "Could not initialize WebGPU!" << std::endl;
        return 1;
    }

    // display the object (WGPUInstance is a simple pointer, it may be copied around without worrying about its size)
    std::cout << "WGPU instance: " << instance << std::endl;

    std::cout << "Requesting adapter..." << std::endl;
    WGPURequestAdapterOptions adapterOpts = {};
    adapterOpts.nextInChain               = nullptr;
    WGPUAdapter adapter                   = Utils::requestAdapterSync(instance, &adapterOpts);
    std::cout << "Got adapter: " << adapter << std::endl;

    std::cout << "Requesting device..." << std::endl;
    WGPUDeviceDescriptor deviceDesc     = {};
    deviceDesc.nextInChain              = nullptr;
    deviceDesc.label                    = "My Device";
    deviceDesc.requiredFeatureCount     = 0;
    deviceDesc.requiredLimits           = nullptr;
    deviceDesc.defaultQueue.nextInChain = nullptr;
    deviceDesc.defaultQueue.label       = "The default queue";
    deviceDesc.deviceLostCallback       = [](WGPUDeviceLostReason reason, const char* message, void* /* pUserData */)
    {
        std::cout << "Device lost: reason " << reason;
        if (message)
        {
            std::cout << " (" << message << ")";
        }
        std::cout << std::endl;
    };
    WGPUDevice device = Utils::requestDeviceSync(adapter, &deviceDesc);
    std::cout << "Got device: " << device << std::endl;

    // display some information about adapter
    Utils::inspectAdapter(adapter);

    // clean up the WebGPU instance
    wgpuInstanceRelease(instance);

    wgpuAdapterRelease(adapter);

    // Display information about the device
    Utils::inspectDevice(device);

    wgpuDeviceRelease(device);

    return 0;
}
