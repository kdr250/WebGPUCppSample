#include "Application.h"

#define WEBGPU_CPP_IMPLEMENTATION
#include <webgpu/webgpu.hpp>

#include <GLFW/glfw3.h>
#include <glfw3webgpu.h>

#ifdef __EMSCRIPTEN__
    #include <emscripten.h>
#endif  // __EMSCRIPTEN__

#include <cassert>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

struct Application::AppData
{
public:
    AppData() {}

    // We put here all the variables that are shared between init and main loop
    GLFWwindow* window;
    wgpu::Device device;
    wgpu::Queue queue;
    wgpu::Surface surface;
    wgpu::Buffer vertexBuffer;
    uint32_t vertexCount;
    std::unique_ptr<wgpu::ErrorCallback> uncapturedErrorCallbackHandle;
    wgpu::TextureFormat surfaceFormat = wgpu::TextureFormat::Undefined;
    wgpu::RenderPipeline pipeline;
};

Application::Application()
{
    data = new AppData();
}

Application::~Application()
{
    delete data;
}

bool Application::Initialize()
{
    // Open window
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    data->window = glfwCreateWindow(640, 480, "Learn WebGPU", nullptr, nullptr);

    wgpu::Instance instance = wgpuCreateInstance(nullptr);

    data->surface = glfwGetWGPUSurface(instance, data->window);

    std::cout << "Requesting adapter..." << std::endl;
    wgpu::RequestAdapterOptions adapterOpts = {};
    adapterOpts.compatibleSurface           = data->surface;
    wgpu::Adapter adapter                   = instance.requestAdapter(adapterOpts);
    std::cout << "Got adapter: " << adapter << std::endl;

    instance.release();

    std::cout << "Requesting device..." << std::endl;
    wgpu::DeviceDescriptor deviceDesc   = {};
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
    wgpu::RequiredLimits requiredLimits = GetRequiredLimits(adapter);
    deviceDesc.requiredLimits           = &requiredLimits;
    data->device                        = adapter.requestDevice(deviceDesc);
    std::cout << "Got device: " << data->device << std::endl;

    data->uncapturedErrorCallbackHandle = data->device.setUncapturedErrorCallback(
        [](wgpu::ErrorType type, char const* message)
        {
            std::cout << "Uncaptured device error: type " << type;
            if (message)
                std::cout << " (" << message << ")";
            std::cout << std::endl;
        });

    data->queue = data->device.getQueue();

    // Configure the surface
    wgpu::SurfaceConfiguration config = {};

    // Configuration of the textures created for the underlying swap chain
    config.width        = 640;
    config.height       = 480;
    config.usage        = wgpu::TextureUsage::RenderAttachment;
    data->surfaceFormat = data->surface.getPreferredFormat(adapter);
    config.format       = data->surfaceFormat;

    // And we do not need any particular view format:
    config.viewFormatCount = 0;
    config.viewFormats     = nullptr;
    config.device          = data->device;
    config.presentMode     = wgpu::PresentMode::Fifo;
    config.alphaMode       = wgpu::CompositeAlphaMode::Auto;

    data->surface.configure(config);

    // Release the adapter only after it has been fully utilized
    adapter.release();

    InitializePipeline();

    InitializeBuffers();

    return true;
}

void Application::Terminate()
{
    data->vertexBuffer.release();
    data->pipeline.release();
    data->surface.unconfigure();
    data->queue.release();
    data->surface.release();
    data->device.release();
    glfwDestroyWindow(data->window);
    glfwTerminate();
}

void Application::MainLoop()
{
    glfwPollEvents();

    // Get the next target texture view
    wgpu::TextureView targetView = GetNextSurfaceTextureView();
    if (!targetView)
        return;

    // Create a command encoder for the draw call
    wgpu::CommandEncoderDescriptor encoderDesc = {};
    encoderDesc.label                          = "My command encoder";
    wgpu::CommandEncoder encoder               = data->device.createCommandEncoder(encoderDesc);

    // Create the render pass that clears the screen with our color
    wgpu::RenderPassDescriptor renderPassDesc = {};

    // The attachment part of the render pass descriptor describes the target texture of the pass
    wgpu::RenderPassColorAttachment renderPassColorAttachment = {};
    renderPassColorAttachment.view                            = targetView;
    renderPassColorAttachment.resolveTarget                   = nullptr;
    renderPassColorAttachment.loadOp                          = wgpu::LoadOp::Clear;
    renderPassColorAttachment.storeOp                         = wgpu::StoreOp::Store;
    renderPassColorAttachment.clearValue                      = WGPUColor {0.9, 0.1, 0.2, 1.0};
#ifndef WEBGPU_BACKEND_WGPU
    renderPassColorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
#endif  // NOT WEBGPU_BACKEND_WGPU

    renderPassDesc.colorAttachmentCount   = 1;
    renderPassDesc.colorAttachments       = &renderPassColorAttachment;
    renderPassDesc.depthStencilAttachment = nullptr;
    renderPassDesc.timestampWrites        = nullptr;

    // Create the render pass and end it immediately (we only clear the screen but do not draw anything)
    wgpu::RenderPassEncoder renderPass = encoder.beginRenderPass(renderPassDesc);
    renderPass.setPipeline(data->pipeline);
    renderPass.draw(3, 1, 0, 0);
    renderPass.end();
    renderPass.release();

    // Finally encode and submit the render pass
    wgpu::CommandBufferDescriptor cmdBufferDescriptor = {};
    cmdBufferDescriptor.label                         = "Command buffer";
    wgpu::CommandBuffer command                       = encoder.finish(cmdBufferDescriptor);
    encoder.release();

    std::cout << "Submitting command..." << std::endl;
    data->queue.submit(1, &command);
    command.release();
    std::cout << "Command submitted." << std::endl;

    // At the enc of the frame
    targetView.release();
#ifndef __EMSCRIPTEN__
    data->surface.present();
#endif

#if defined(WEBGPU_BACKEND_DAWN)
    data->device.tick();
#elif defined(WEBGPU_BACKEND_WGPU)
    device->poll(false);
#endif
}

bool Application::IsRunning()
{
    return !glfwWindowShouldClose(data->window);
}

wgpu::TextureView Application::GetNextSurfaceTextureView()
{
    // Get the surface texture
    wgpu::SurfaceTexture surfaceTexture;
    data->surface.getCurrentTexture(&surfaceTexture);
    if (surfaceTexture.status != wgpu::SurfaceGetCurrentTextureStatus::Success)
    {
        return nullptr;
    }
    wgpu::Texture texture = surfaceTexture.texture;

    // Create a view for this surface texture
    wgpu::TextureViewDescriptor viewDescriptor;
    viewDescriptor.label           = "Surface texture view";
    viewDescriptor.format          = texture.getFormat();
    viewDescriptor.dimension       = wgpu::TextureViewDimension::_2D;
    viewDescriptor.baseMipLevel    = 0;
    viewDescriptor.mipLevelCount   = 1;
    viewDescriptor.baseArrayLayer  = 0;
    viewDescriptor.arrayLayerCount = 1;
    viewDescriptor.aspect          = wgpu::TextureAspect::All;
    wgpu::TextureView targetView   = texture.createView(viewDescriptor);

    return targetView;
}

void Application::InitializePipeline()
{
    std::ifstream shaderFile("resources/shader/sample.wgsl");
    if (!shaderFile.is_open())
    {
        std::cerr << "failed to open file" << std::endl;
        return;
    }
    std::stringstream sstream;
    sstream << shaderFile.rdbuf();
    std::string contents     = sstream.str();
    const char* shaderSource = contents.c_str();

    wgpu::ShaderModuleDescriptor shaderDesc;

    wgpu::ShaderModuleWGSLDescriptor shaderCodeDesc;
    shaderCodeDesc.chain.next       = nullptr;
    shaderCodeDesc.chain.sType      = wgpu::SType::ShaderModuleWGSLDescriptor;
    shaderDesc.nextInChain          = &shaderCodeDesc.chain;
    shaderCodeDesc.code             = shaderSource;
    wgpu::ShaderModule shaderModule = data->device.createShaderModule(shaderDesc);

    wgpu::RenderPipelineDescriptor pipelineDesc;
    pipelineDesc.vertex.bufferCount         = 0;
    pipelineDesc.vertex.buffers             = nullptr;
    pipelineDesc.vertex.module              = shaderModule;
    pipelineDesc.vertex.entryPoint          = "vs_main";
    pipelineDesc.vertex.constantCount       = 0;
    pipelineDesc.vertex.constants           = nullptr;
    pipelineDesc.primitive.topology         = wgpu::PrimitiveTopology::TriangleList;
    pipelineDesc.primitive.stripIndexFormat = wgpu::IndexFormat::Undefined;
    pipelineDesc.primitive.frontFace        = wgpu::FrontFace::CCW;
    pipelineDesc.primitive.cullMode         = wgpu::CullMode::None;

    wgpu::VertexBufferLayout vertexBufferLayout;
    wgpu::VertexAttribute positionAttrib;

    positionAttrib.shaderLocation = 0;                              // Corresponds to @location(...)
    positionAttrib.format         = wgpu::VertexFormat::Float32x2;  // Means vec2f in the shader
    positionAttrib.offset         = 0;                              // Index of the first element

    vertexBufferLayout.attributeCount = 1;
    vertexBufferLayout.attributes     = &positionAttrib;
    vertexBufferLayout.arrayStride    = 2 * sizeof(float);
    vertexBufferLayout.stepMode       = wgpu::VertexStepMode::Vertex;

    pipelineDesc.vertex.bufferCount = 1;
    pipelineDesc.vertex.buffers     = &vertexBufferLayout;

    wgpu::FragmentState fragmentState;
    fragmentState.module        = shaderModule;
    fragmentState.entryPoint    = "fs_main";
    fragmentState.constantCount = 0;
    fragmentState.constants     = nullptr;

    wgpu::BlendState blendState;
    blendState.color.srcFactor = wgpu::BlendFactor::SrcAlpha;
    blendState.color.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
    blendState.color.operation = wgpu::BlendOperation::Add;
    blendState.alpha.srcFactor = wgpu::BlendFactor::Zero;
    blendState.alpha.dstFactor = wgpu::BlendFactor::One;
    blendState.alpha.operation = wgpu::BlendOperation::Add;

    wgpu::ColorTargetState colorTarget;
    colorTarget.format    = data->surfaceFormat;
    colorTarget.blend     = &blendState;
    colorTarget.writeMask = wgpu::ColorWriteMask::All;

    fragmentState.targetCount = 1;
    fragmentState.targets     = &colorTarget;

    pipelineDesc.fragment = &fragmentState;

    pipelineDesc.depthStencil = nullptr;

    pipelineDesc.multisample.count                  = 1;
    pipelineDesc.multisample.mask                   = ~0u;
    pipelineDesc.multisample.alphaToCoverageEnabled = false;
    pipelineDesc.layout                             = nullptr;

    data->pipeline = data->device.createRenderPipeline(pipelineDesc);

    shaderModule.release();
}

// We define a function that hides implementation-specific variants of device polling:
void wgpuPollEvent([[maybe_unused]] wgpu::Device device, [[maybe_unused]] bool yieldToWebBrowser)
{
#if defined(WEBGPU_BACKEND_DAWN)
    device.tick();
#elif defined(WEBGPU_BACKEND_WGPU)
    device.poll(false);
#elif defined(WEBGPU_BACKEND_EMSCRIPTEN)
    if (yieldToWebBrowser)
    {
        emscripten_sleep(100);
    }
#endif
}

void Application::InitializeBuffers()
{
    // Vertex buffer data
    // There are 2 floats per vertex, one for x and one for y.
    // But in the end this is just a bunch of floats to the eyes of the GPU,
    // the *layout* will tell how to interpret this.
    std::vector<float> vertexData = {-0.5, -0.5, 0.5, -0.5, 0.0, 0.5};

    // we will declare vertexCount as a member of the Application class
    data->vertexCount = static_cast<uint32_t>(vertexData.size());

    // Create vertex buffer
    wgpu::BufferDescriptor bufferDesc;
    bufferDesc.size             = vertexData.size() * sizeof(float);
    bufferDesc.usage            = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Vertex;
    bufferDesc.mappedAtCreation = false;
    data->vertexBuffer          = data->device.createBuffer(bufferDesc);

    // Upload geometry data to the buffer
    data->queue.writeBuffer(data->vertexBuffer, 0, vertexData.data(), bufferDesc.size);
}

wgpu::RequiredLimits Application::GetRequiredLimits(wgpu::Adapter adapter) const
{
    // Get adapter supported limits, in case we need them
    wgpu::SupportedLimits supportedLimits;
    adapter.getLimits(&supportedLimits);

    wgpu::RequiredLimits requiredLimits = wgpu::Default;
    // We use at most 1 vertex attribute for now
    requiredLimits.limits.maxVertexAttributes = 1;
    // We should also tell that we use 1 vertex buffers
    requiredLimits.limits.maxVertexBuffers = 1;
    // Maximum size of a buffer is 6 vertices of 2 float each
    requiredLimits.limits.maxBufferSize = 6 * 2 * sizeof(float);
    // Maximum stride between 2 consecutive vertices in the vertex buffer
    requiredLimits.limits.maxVertexBufferArrayStride = 2 * sizeof(float);

    // These two limits are different because they are "minimum" limits,
    // they are the only ones we are may forward from the adapter's supported
    // limits.
    requiredLimits.limits.minUniformBufferOffsetAlignment = supportedLimits.limits.minUniformBufferOffsetAlignment;
    requiredLimits.limits.minUniformBufferOffsetAlignment = supportedLimits.limits.minUniformBufferOffsetAlignment;

    return requiredLimits;
}
