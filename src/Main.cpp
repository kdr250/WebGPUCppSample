#include <webgpu/webgpu.h>
#include <iostream>

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

    // clean up the WebGPU instance
    wgpuInstanceRelease(instance);

    return 0;
}
