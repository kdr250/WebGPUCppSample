#include <webgpu/webgpu.h>
#include <cassert>
#include <iostream>
#include <vector>

/**
 * Utility function to get a WebGPU adapter, so that
 * WGPUAdapter adapter = requestAdapterSync(options);
 */
WGPUAdapter requestAdapterSync(WGPUInstance instance, const WGPURequestAdapterOptions* options)
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
    WGPUAdapter adapter                   = requestAdapterSync(instance, &adapterOpts);
    std::cout << "Got adapter: " << adapter << std::endl;

    // clean up the WebGPU instance
    wgpuInstanceRelease(instance);

    wgpuAdapterRelease(adapter);

    return 0;
}
