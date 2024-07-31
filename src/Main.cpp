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
    WGPUDeviceDescriptor deviceDesc = {};
    WGPUDevice device               = Utils::requestDeviceSync(adapter, &deviceDesc);
    std::cout << "Got device: " << device << std::endl;

    // display some information about adapter
    Utils::inspectAdapter(adapter);

    // clean up the WebGPU instance
    wgpuInstanceRelease(instance);

    wgpuAdapterRelease(adapter);

    wgpuDeviceRelease(device);

    return 0;
}
