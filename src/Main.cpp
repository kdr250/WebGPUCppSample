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

    // display some information about adapter
    Utils::inspectAdapter(adapter);

    // We no longer need to use the instance once we have the adapter
    wgpuInstanceRelease(instance);

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

    // A function that is invoked whenever there is an error in the use of the device
    auto onDeviceError = [](WGPUErrorType type, const char* message, void* /* pUserData */)
    {
        std::cout << "Uncaptured device error: type " << type;
        if (message)
        {
            std::cout << " (" << message << ")";
        }
        std::cout << std::endl;
    };
    wgpuDeviceSetUncapturedErrorCallback(device, onDeviceError, nullptr);

    // We no longer need to access the adapter once we have the device
    wgpuAdapterRelease(adapter);

    // Display information about the device
    Utils::inspectDevice(device);

    WGPUQueue queue = wgpuDeviceGetQueue(device);

    // Add a callback to monitor the moment queued work finished
    auto onQueueWorkDone = [](WGPUQueueWorkDoneStatus status, void* /* pUserData */)
    {
        std::cout << "Queued work finished with status: " << status << std::endl;
    };
    wgpuQueueOnSubmittedWorkDone(queue, onQueueWorkDone, nullptr /* pUserData */);

    wgpuQueueRelease(queue);
    wgpuDeviceRelease(device);

    return 0;
}
