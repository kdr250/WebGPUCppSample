#include "Application.h"
#include <glfw3webgpu.h>
#include <iostream>
#include "WebGpuUtils.h"

bool Application::Initialize()
{
    // Open window
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    window = glfwCreateWindow(640, 480, "Learn WebGPU", nullptr, nullptr);

    WGPUInstance instance = wgpuCreateInstance(nullptr);

    std::cout << "Requesting adapter..." << std::endl;
    surface                               = glfwGetWGPUSurface(instance, window);
    WGPURequestAdapterOptions adapterOpts = {};
    adapterOpts.nextInChain               = nullptr;
    adapterOpts.compatibleSurface         = surface;
    WGPUAdapter adapter                   = Utils::requestAdapterSync(instance, &adapterOpts);
    std::cout << "Got adapter: " << adapter << std::endl;

    wgpuInstanceRelease(instance);

    std::cout << "Requesting device..." << std::endl;
    WGPUDeviceDescriptor deviceDesc     = {};
    deviceDesc.nextInChain              = nullptr;
    deviceDesc.label                    = "My Device";
    deviceDesc.requiredFeatureCount     = 0;
    deviceDesc.requiredLimits           = nullptr;
    deviceDesc.defaultQueue.nextInChain = nullptr;
    deviceDesc.defaultQueue.label       = "The default queue";
    deviceDesc.deviceLostCallback       = [](WGPUDeviceLostReason reason, char const* message, void* /* pUserData */)
    {
        std::cout << "Device lost: reason " << reason;
        if (message)
            std::cout << " (" << message << ")";
        std::cout << std::endl;
    };
    device = Utils::requestDeviceSync(adapter, &deviceDesc);
    std::cout << "Got device: " << device << std::endl;

    auto onDeviceError = [](WGPUErrorType type, char const* message, void* /* pUserData */)
    {
        std::cout << "Uncaptured device error: type " << type;
        if (message)
            std::cout << " (" << message << ")";
        std::cout << std::endl;
    };
    wgpuDeviceSetUncapturedErrorCallback(device, onDeviceError, nullptr /* pUserData */);

    queue = wgpuDeviceGetQueue(device);

    // Configure the surface
    WGPUSurfaceConfiguration config = {};
    config.nextInChain              = nullptr;

    // Configuration of the textures created for the underlying swap chain
    config.width                    = 640;
    config.height                   = 480;
    config.usage                    = WGPUTextureUsage_RenderAttachment;
    WGPUTextureFormat surfaceFormat = wgpuSurfaceGetPreferredFormat(surface, adapter);
    config.format                   = surfaceFormat;

    // And we do not need any particular view format:
    config.viewFormatCount = 0;
    config.viewFormats     = nullptr;
    config.device          = device;
    config.presentMode     = WGPUPresentMode_Fifo;
    config.alphaMode       = WGPUCompositeAlphaMode_Auto;

    wgpuSurfaceConfigure(surface, &config);

    // Release the adapter only after it has been fully utilized
    wgpuAdapterRelease(adapter);

    return true;
}

void Application::Terminate()
{
    wgpuSurfaceUnconfigure(surface);
    wgpuQueueRelease(queue);
    wgpuSurfaceRelease(surface);
    wgpuDeviceRelease(device);
    glfwDestroyWindow(window);
    glfwTerminate();
}

void Application::MainLoop()
{
    glfwPollEvents();

    // Get the next target texture view
    WGPUTextureView targetView = GetNextSurfaceTextureView();
    if (!targetView)
        return;

    // Create a command encoder for the draw call
    WGPUCommandEncoderDescriptor encoderDesc = {};
    encoderDesc.nextInChain                  = nullptr;
    encoderDesc.label                        = "My command encoder";
    WGPUCommandEncoder encoder               = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);

    // Create the render pass that clears the screen with our color
    WGPURenderPassDescriptor renderPassDesc = {};
    renderPassDesc.nextInChain              = nullptr;

    // The attachment part of the render pass descriptor describes the target texture of the pass
    WGPURenderPassColorAttachment renderPassColorAttachment = {};
    renderPassColorAttachment.view                          = targetView;
    renderPassColorAttachment.resolveTarget                 = nullptr;
    renderPassColorAttachment.loadOp                        = WGPULoadOp_Clear;
    renderPassColorAttachment.storeOp                       = WGPUStoreOp_Store;
    renderPassColorAttachment.clearValue                    = WGPUColor {0.9, 0.1, 0.2, 1.0};

    renderPassDesc.colorAttachmentCount   = 1;
    renderPassDesc.colorAttachments       = &renderPassColorAttachment;
    renderPassDesc.depthStencilAttachment = nullptr;
    renderPassDesc.timestampWrites        = nullptr;

    // Create the render pass and end it immediately (we only clear the screen but do not draw anything)
    WGPURenderPassEncoder renderPass = wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);
    wgpuRenderPassEncoderEnd(renderPass);
    wgpuRenderPassEncoderRelease(renderPass);

    // Finally encode and submit the render pass
    WGPUCommandBufferDescriptor cmdBufferDescriptor = {};
    cmdBufferDescriptor.nextInChain                 = nullptr;
    cmdBufferDescriptor.label                       = "Command buffer";
    WGPUCommandBuffer command                       = wgpuCommandEncoderFinish(encoder, &cmdBufferDescriptor);
    wgpuCommandEncoderRelease(encoder);

    std::cout << "Submitting command..." << std::endl;
    wgpuQueueSubmit(queue, 1, &command);
    wgpuCommandBufferRelease(command);
    std::cout << "Command submitted." << std::endl;

    // At the end of the frame
    wgpuTextureViewRelease(targetView);

    wgpuDeviceTick(device);
}

bool Application::IsRunning()
{
    return !glfwWindowShouldClose(window);
}

WGPUTextureView Application::GetNextSurfaceTextureView()
{
    // Get the surface texture
    WGPUSurfaceTexture surfaceTexture;
    wgpuSurfaceGetCurrentTexture(surface, &surfaceTexture);
    if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_Success)
    {
        return nullptr;
    }

    // Create a view for this surface texture
    WGPUTextureViewDescriptor viewDescriptor;
    viewDescriptor.nextInChain     = nullptr;
    viewDescriptor.label           = "Surface texture view";
    viewDescriptor.format          = wgpuTextureGetFormat(surfaceTexture.texture);
    viewDescriptor.dimension       = WGPUTextureViewDimension_2D;
    viewDescriptor.baseMipLevel    = 0;
    viewDescriptor.mipLevelCount   = 1;
    viewDescriptor.baseArrayLayer  = 0;
    viewDescriptor.arrayLayerCount = 1;
    viewDescriptor.aspect          = WGPUTextureAspect_All;
    WGPUTextureView targetView     = wgpuTextureCreateView(surfaceTexture.texture, &viewDescriptor);

    return targetView;
}
