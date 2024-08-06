#include "Application.h"

#define WEBGPU_CPP_IMPLEMENTATION
#include <webgpu/webgpu.hpp>

#include <GLFW/glfw3.h>
#include <glfw3webgpu.h>
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_LEFT_HANDED
#include <glm/ext.hpp>
#include <glm/glm.hpp>
#include <magic_enum.hpp>
#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#ifdef __EMSCRIPTEN__
    #include <emscripten.h>
#endif  // __EMSCRIPTEN__

#include <array>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

constexpr float PI = 3.14159265358979323846f;

/**
 * A structure that describes the data layout in the vertex buffer
 * We do not instantiate it but use it in `sizeof` and `offsetof`
 */
struct VertexAttributes
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 color;
};

struct MyUniforms
{
    glm::mat4x4 projectionMatrix;
    glm::mat4x4 viewMatrix;
    glm::mat4x4 modelMatrix;
    glm::vec4 color;
    float time;
    float _pat[3];
};

static_assert(sizeof(MyUniforms) % 16 == 0);

wgpu::ShaderModule loadShaderModule(const fs::path& path, wgpu::Device device);
bool loadGeometryFromObj(const fs::path& path, std::vector<VertexAttributes>& vertexData);

struct Application::AppData
{
public:
    AppData() {}

    // We put here all the variables that are shared between init and main loop
    GLFWwindow* window;
    wgpu::Device device;
    wgpu::Queue queue;
    wgpu::Surface surface;
    wgpu::Buffer pointBuffer;
    wgpu::Buffer indexBuffer;
    wgpu::Buffer uniformBuffer;
    uint32_t indexCount;
    std::unique_ptr<wgpu::ErrorCallback> uncapturedErrorCallbackHandle;
    wgpu::TextureFormat surfaceFormat = wgpu::TextureFormat::Undefined;
    wgpu::RenderPipeline pipeline;
    wgpu::BindGroupLayout bindGroupLayout;
    wgpu::BindGroupLayoutDescriptor bindGroupLayoutDesc;
    wgpu::BindGroup bindGroup;
    wgpu::Texture depthTexture;
    wgpu::TextureView depthTextureView;

    MyUniforms uniforms;

    glm::mat4x4 T1;
    glm::mat4x4 S;
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
    config.width  = 640;
    config.height = 480;
    config.usage  = wgpu::TextureUsage::RenderAttachment;
#ifdef WEBGPU_BACKEND_WGPU
    data->surfaceFormat = surface.getPreferredFormat(adapter);
#else
    data->surfaceFormat = wgpu::TextureFormat::BGRA8Unorm;
#endif
    config.format = data->surfaceFormat;
    std::cout << "Swapchain format: " << magic_enum::enum_name<WGPUTextureFormat>(data->surfaceFormat) << std::endl;

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
    data->depthTextureView.release();
    data->depthTexture.destroy();
    data->depthTexture.release();
    data->pointBuffer.destroy();
    data->pointBuffer.release();
    data->indexBuffer.destroy();
    data->indexBuffer.release();
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

    // Update uniform buffer. Only update 1st float of the buffer
    data->uniforms.time = static_cast<float>(glfwGetTime());
    data->queue.writeBuffer(data->uniformBuffer,
                            offsetof(MyUniforms, time),
                            &data->uniforms.time,
                            sizeof(MyUniforms::time));

    // Update view matrix
    float angle                = data->uniforms.time;
    glm::mat4x4 R1             = glm::rotate(glm::mat4x4(1.0), angle, glm::vec3(0.0, 0.0, 1.0));
    data->uniforms.modelMatrix = R1 * data->T1 * data->S;
    data->queue.writeBuffer(data->uniformBuffer,
                            offsetof(MyUniforms, modelMatrix),
                            &data->uniforms.modelMatrix,
                            sizeof(MyUniforms::modelMatrix));

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
    renderPassColorAttachment.clearValue                      = WGPUColor {0.05, 0.05, 0.05, 1.0};
#ifndef WEBGPU_BACKEND_WGPU
    renderPassColorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
#endif  // NOT WEBGPU_BACKEND_WGPU

    renderPassDesc.colorAttachmentCount = 1;
    renderPassDesc.colorAttachments     = &renderPassColorAttachment;

    // We now add a depth/stencil attachment:
    wgpu::RenderPassDepthStencilAttachment depthStencilAttachment;
    depthStencilAttachment.view              = data->depthTextureView;
    depthStencilAttachment.depthClearValue   = 1.0f;
    depthStencilAttachment.depthLoadOp       = wgpu::LoadOp::Clear;
    depthStencilAttachment.depthStoreOp      = wgpu::StoreOp::Store;
    depthStencilAttachment.depthReadOnly     = false;
    depthStencilAttachment.stencilClearValue = 0;
#ifdef WEBGPU_BACKEND_WGPU
    depthStencilAttachment.stencilLoadOp  = wgpu::LoadOp::Clear;
    depthStencilAttachment.stencilStoreOp = wgpu::StoreOp::Store;
#else
    depthStencilAttachment.stencilLoadOp  = wgpu::LoadOp::Undefined;
    depthStencilAttachment.stencilStoreOp = wgpu::StoreOp::Undefined;
#endif
    depthStencilAttachment.stencilReadOnly = true;
    renderPassDesc.depthStencilAttachment  = &depthStencilAttachment;
    renderPassDesc.timestampWrites         = nullptr;

    // Create the render pass and end it immediately (we only clear the screen but do not draw anything)
    wgpu::RenderPassEncoder renderPass = encoder.beginRenderPass(renderPassDesc);
    renderPass.setPipeline(data->pipeline);

    // Set both vertex and index buffers
    renderPass.setVertexBuffer(0, data->pointBuffer, 0, data->pointBuffer.getSize());
    renderPass.setIndexBuffer(data->indexBuffer, wgpu::IndexFormat::Uint16, 0, data->indexBuffer.getSize());

    // Set binding group
    renderPass.setBindGroup(0, data->bindGroup, 0, nullptr);
    renderPass.drawIndexed(data->indexCount, 1, 0, 0, 0);

    renderPass.end();
    renderPass.release();

    // Finally encode and submit the render pass
    wgpu::CommandBufferDescriptor cmdBufferDescriptor = {};
    cmdBufferDescriptor.label                         = "Command buffer";
    wgpu::CommandBuffer command                       = encoder.finish(cmdBufferDescriptor);
    encoder.release();

    data->queue.submit(1, &command);
    command.release();

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
    std::cout << "Creating shader module..." << std::endl;
    wgpu::ShaderModule shaderModule = loadShaderModule("resources/shader/sample.wgsl", data->device);
    std::cout << "Shader module: " << shaderModule << std::endl;

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

    wgpu::VertexBufferLayout pointBufferLayout;
    std::vector<wgpu::VertexAttribute> vertexAttribs(3);

    // Describe the position attribute
    vertexAttribs[0].shaderLocation = 0;  // @location(0)
    vertexAttribs[0].format         = wgpu::VertexFormat::Float32x3;
    vertexAttribs[0].offset         = offsetof(VertexAttributes, position);

    // Describe the normal attribute
    vertexAttribs[1].shaderLocation = 1;  // @location(1)
    vertexAttribs[1].format         = wgpu::VertexFormat::Float32x3;
    vertexAttribs[1].offset         = offsetof(VertexAttributes, normal);

    // Describe the color attribute
    vertexAttribs[2].shaderLocation = 2;  // @location(2)
    vertexAttribs[2].format         = wgpu::VertexFormat::Float32x3;
    vertexAttribs[2].offset         = offsetof(VertexAttributes, color);

    pointBufferLayout.attributeCount = static_cast<uint32_t>(vertexAttribs.size());
    pointBufferLayout.attributes     = vertexAttribs.data();
    pointBufferLayout.arrayStride    = sizeof(VertexAttributes);
    pointBufferLayout.stepMode       = wgpu::VertexStepMode::Vertex;

    pipelineDesc.vertex.bufferCount = 1;
    pipelineDesc.vertex.buffers     = &pointBufferLayout;

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

    // Setup a depth buffer state for the render pipeline
    wgpu::DepthStencilState depthStencilState = wgpu::Default;
    depthStencilState.depthCompare            = wgpu::CompareFunction::Less;
    depthStencilState.depthWriteEnabled       = true;
    wgpu::TextureFormat depthTextureFormat    = wgpu::TextureFormat::Depth24Plus;
    depthStencilState.format                  = depthTextureFormat;
    depthStencilState.stencilReadMask         = 0;
    depthStencilState.stencilWriteMask        = 0;
    pipelineDesc.depthStencil                 = &depthStencilState;

    pipelineDesc.multisample.count                  = 1;
    pipelineDesc.multisample.mask                   = ~0u;
    pipelineDesc.multisample.alphaToCoverageEnabled = false;

    // Create binding layout
    wgpu::BindGroupLayoutEntry bindingLayout = wgpu::Default;
    bindingLayout.binding                    = 0;
    bindingLayout.visibility                 = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
    bindingLayout.buffer.type                = wgpu::BufferBindingType::Uniform;
    bindingLayout.buffer.minBindingSize      = sizeof(MyUniforms);

    // Create a bind group layout
    wgpu::BindGroupLayoutDescriptor bindGroupLayoutDesc;
    bindGroupLayoutDesc.entryCount = 1;
    bindGroupLayoutDesc.entries    = &bindingLayout;
    data->bindGroupLayoutDesc      = bindGroupLayoutDesc;
    data->bindGroupLayout          = data->device.createBindGroupLayout(data->bindGroupLayoutDesc);

    // Create the pipeline layout
    wgpu::PipelineLayoutDescriptor layoutDesc;
    layoutDesc.bindGroupLayoutCount = 1;
    layoutDesc.bindGroupLayouts     = (WGPUBindGroupLayout*)&data->bindGroupLayout;
    wgpu::PipelineLayout layout     = data->device.createPipelineLayout(layoutDesc);

    pipelineDesc.layout = layout;

    data->pipeline = data->device.createRenderPipeline(pipelineDesc);
    std::cout << "Render pipeline: " << data->pipeline << std::endl;

    // Create the depth texture
    wgpu::TextureDescriptor depthTextureDesc;
    depthTextureDesc.dimension       = wgpu::TextureDimension::_2D;
    depthTextureDesc.format          = depthTextureFormat;
    depthTextureDesc.mipLevelCount   = 1;
    depthTextureDesc.sampleCount     = 1;
    depthTextureDesc.size            = {640, 480, 1};
    depthTextureDesc.usage           = wgpu::TextureUsage::RenderAttachment;
    depthTextureDesc.viewFormatCount = 1;
    depthTextureDesc.viewFormats     = (WGPUTextureFormat*)&depthTextureFormat;
    data->depthTexture               = data->device.createTexture(depthTextureDesc);

    // Create the view of the depth texture manipulated by the rasterizer
    wgpu::TextureViewDescriptor depthTextureViewDesc;
    depthTextureViewDesc.aspect          = wgpu::TextureAspect::DepthOnly;
    depthTextureViewDesc.baseArrayLayer  = 0;
    depthTextureViewDesc.arrayLayerCount = 1;
    depthTextureViewDesc.baseMipLevel    = 0;
    depthTextureViewDesc.mipLevelCount   = 1;
    depthTextureViewDesc.dimension       = wgpu::TextureViewDimension::_2D;
    depthTextureViewDesc.format          = depthTextureFormat;
    data->depthTextureView               = data->depthTexture.createView(depthTextureViewDesc);

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
    std::vector<float> pointData;
    std::vector<uint16_t> indexData;

    bool success = loadGeometryFromObj("resources/shader/pyramid.obj", pointData, indexData, 6);
    assert(success && "Could not load geometry!");

    // we will declare indexCount as a member of the Application class
    data->indexCount = static_cast<uint32_t>(indexData.size());

    // Create point buffer
    wgpu::BufferDescriptor bufferDesc;
    bufferDesc.size             = pointData.size() * sizeof(float);
    bufferDesc.usage            = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Vertex;
    bufferDesc.mappedAtCreation = false;
    data->pointBuffer           = data->device.createBuffer(bufferDesc);

    data->queue.writeBuffer(data->pointBuffer, 0, pointData.data(), bufferDesc.size);

    // Create index buffer
    bufferDesc.size   = indexData.size() * sizeof(uint16_t);
    bufferDesc.size   = (bufferDesc.size + 3) & ~3;  // round up to the next multiple of 4
    bufferDesc.usage  = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Index;
    data->indexBuffer = data->device.createBuffer(bufferDesc);

    data->queue.writeBuffer(data->indexBuffer, 0, indexData.data(), bufferDesc.size);

    // Create uniform buffer.
    bufferDesc.size             = sizeof(MyUniforms);
    bufferDesc.usage            = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Uniform;
    bufferDesc.mappedAtCreation = false;
    data->uniformBuffer         = data->device.createBuffer(bufferDesc);

    // Upload the initial value of the uniforms
    data->uniforms = createUniforms();
    data->queue.writeBuffer(data->uniformBuffer, 0, &data->uniforms, sizeof(MyUniforms));

    // Create a binding
    wgpu::BindGroupEntry binding;
    binding.binding = 0;
    binding.buffer  = data->uniformBuffer;
    binding.offset  = 0;
    binding.size    = sizeof(MyUniforms);

    // A bind group contains one or multiple bindings
    wgpu::BindGroupDescriptor bindGroupDesc;
    bindGroupDesc.layout     = data->bindGroupLayout;
    bindGroupDesc.entryCount = data->bindGroupLayoutDesc.entryCount;
    bindGroupDesc.entries    = &binding;
    data->bindGroup          = data->device.createBindGroup(bindGroupDesc);
}

wgpu::RequiredLimits Application::GetRequiredLimits(wgpu::Adapter adapter) const
{
    // Get adapter supported limits, in case we need them
    wgpu::SupportedLimits supportedLimits;
    adapter.getLimits(&supportedLimits);

    wgpu::RequiredLimits requiredLimits = wgpu::Default;
    // We use at most 1 vertex attribute for now
    requiredLimits.limits.maxVertexAttributes = 3;
    // We should also tell that we use 1 vertex buffers
    requiredLimits.limits.maxVertexBuffers = 1;
    // Maximum size of a buffer is 6 vertices of 2 float each
    requiredLimits.limits.maxBufferSize = 16 * sizeof(VertexAttributes);
    // Maximum stride between 2 consecutive vertices in the vertex buffer
    requiredLimits.limits.maxVertexBufferArrayStride = sizeof(VertexAttributes);
    // There is a maximum of 3 float forwarded from vertex to fragment shader
    requiredLimits.limits.maxInterStageShaderComponents = 6;
    // We use at most 1 bind group for now
    requiredLimits.limits.maxBindGroups = 1;
    // We use at most 1 uniform buffer per stage
    requiredLimits.limits.maxUniformBuffersPerShaderStage = 1;
    // Uniform structs have a size of maximum 16 float (more than what we need)
    requiredLimits.limits.maxUniformBufferBindingSize = 16 * 4 * sizeof(float);
    // For the depth buffer, we enable textures (up to the size of the window):
    requiredLimits.limits.maxTextureDimension1D = 480;
    requiredLimits.limits.maxTextureDimension2D = 640;
    requiredLimits.limits.maxTextureArrayLayers = 1;

    // These two limits are different because they are "minimum" limits,
    // they are the only ones we are may forward from the adapter's supported
    // limits.
    requiredLimits.limits.minUniformBufferOffsetAlignment = supportedLimits.limits.minUniformBufferOffsetAlignment;
    requiredLimits.limits.minUniformBufferOffsetAlignment = supportedLimits.limits.minUniformBufferOffsetAlignment;

    return requiredLimits;
}

wgpu::ShaderModule loadShaderModule(const fs::path& path, wgpu::Device device)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        return nullptr;
    }
    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    std::string shaderSource(size, ' ');
    file.seekg(0);
    file.read(shaderSource.data(), size);

    wgpu::ShaderModuleWGSLDescriptor shaderCodeDesc {};
    shaderCodeDesc.chain.next  = nullptr;
    shaderCodeDesc.chain.sType = wgpu::SType::ShaderModuleWGSLDescriptor;
    shaderCodeDesc.code        = shaderSource.c_str();
    wgpu::ShaderModuleDescriptor shaderDesc {};
    shaderDesc.nextInChain = &shaderCodeDesc.chain;
#ifdef WEBGPU_BACKEND_WGPU
    shaderDesc.hintCount = 0;
    shaderDesc.hints     = nullptr;
#endif

    return device.createShaderModule(shaderDesc);
}

bool loadGeometryFromObj(const fs::path& path, std::vector<VertexAttributes>& vertexData)
{
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;

    std::string warn;
    std::string err;

    bool ret = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path.string().c_str());

    if (!warn.empty())
    {
        std::cout << warn << std::endl;
    }

    if (!err.empty())
    {
        std::cerr << err << std::endl;
    }

    if (!ret)
    {
        return false;
    }

    // Filling in vertexData:
    vertexData.clear();
    for (const auto& shape : shapes)
    {
        size_t offset = vertexData.size();
        vertexData.resize(offset + shape.mesh.indices.size());

        for (size_t i = 0; i < shape.mesh.indices.size(); ++i)
        {
            const tinyobj::index_t& idx = shape.mesh.indices[i];

            vertexData[offset + i].position = {
                attrib.vertices[3 * idx.vertex_index + 0],
                -attrib.vertices[3 * idx.vertex_index + 2],  // Add a minus to avoid mirroring
                attrib.vertices[3 * idx.vertex_index + 1]};

            // Also apply the transform to normals!!
            vertexData[offset + i].normal = {attrib.normals[3 * idx.normal_index + 0],
                                             -attrib.normals[3 * idx.normal_index + 2],
                                             attrib.normals[3 * idx.normal_index + 1]};

            vertexData[offset + i].color = {attrib.colors[3 * idx.vertex_index + 0],
                                            attrib.colors[3 * idx.vertex_index + 1],
                                            attrib.colors[3 * idx.vertex_index + 2]};
        }
    }

    return true;
}

MyUniforms Application::createUniforms()
{
    MyUniforms uniforms;

    // Translate the view
    glm::vec3 focalPoint(0.0, 0.0, -2.0);
    glm::mat4x4 T2 = glm::transpose(glm::mat4x4(1.0,
                                                0.0,
                                                0.0,
                                                -focalPoint.x,
                                                0.0,
                                                1.0,
                                                0.0,
                                                -focalPoint.y,
                                                0.0,
                                                0.0,
                                                1.0,
                                                -focalPoint.z,
                                                0.0,
                                                0.0,
                                                0.0,
                                                1.0));

    // Build transform matrices
    // Option A: Manually define matrices
    // Scale the object
    glm::mat4x4 S =
        glm::transpose(glm::mat4x4(0.3, 0.0, 0.0, 0.0, 0.0, 0.3, 0.0, 0.0, 0.0, 0.0, 0.3, 0.0, 0.0, 0.0, 0.0, 1.0));

    // Translate the object
    glm::mat4x4 T1 =
        glm::transpose(glm::mat4x4(1.0, 0.0, 0.0, 0.5, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0));

    // Rotate the object
    float angle1 = 2.0f;  // arbitrary time
    float c1     = glm::cos(angle1);
    float s1     = glm::sin(angle1);
    glm::mat4x4 R1 =
        glm::transpose(glm::mat4x4(c1, s1, 0.0, 0.0, -s1, c1, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0));

    // Rotate the view point
    float angle2 = 3.0f * PI / 4.0f;
    float c2     = glm::cos(angle2);
    float s2     = glm::sin(angle2);
    glm::mat4x4 R2 =
        glm::transpose(glm::mat4x4(1.0, 0.0, 0.0, 0.0, 0.0, c2, s2, 0.0, 0.0, -s2, c2, 0.0, 0.0, 0.0, 0.0, 1.0));

    uniforms.modelMatrix = R1 * T1 * S;
    uniforms.viewMatrix  = T2 * R2;

    float ratio               = 640.0f / 480.0f;
    float focalLength         = 2.0;
    float near                = 0.01f;
    float far                 = 100.0f;
    float divider             = 1 / (focalLength * (far - near));
    uniforms.projectionMatrix = glm::transpose(glm::mat4x4(1.0,
                                                           0.0,
                                                           0.0,
                                                           0.0,
                                                           0.0,
                                                           ratio,
                                                           0.0,
                                                           0.0,
                                                           0.0,
                                                           0.0,
                                                           far * divider,
                                                           -far * near * divider,
                                                           0.0,
                                                           0.0,
                                                           1.0 / focalLength,
                                                           0.0));

    // Option B: Use GLM extensions
    S                    = glm::scale(glm::mat4x4(1.0), glm::vec3(0.3f));
    T1                   = glm::translate(glm::mat4x4(1.0), glm::vec3(0.5, 0.0, 0.0));
    R1                   = glm::rotate(glm::mat4x4(1.0), angle1, glm::vec3(0.0, 0.0, 1.0));
    uniforms.modelMatrix = R1 * T1 * S;

    R2                  = glm::rotate(glm::mat4x4(1.0), -angle2, glm::vec3(1.0, 0.0, 0.0));
    T2                  = glm::translate(glm::mat4x4(1.0), -focalPoint);
    uniforms.viewMatrix = T2 * R2;

    // Option C: A different way of using GLM extensions
    glm::mat4x4 M(1.0);
    M                    = glm::rotate(M, angle1, glm::vec3(0.0, 0.0, 1.0));
    M                    = glm::translate(M, glm::vec3(0.5, 0.0, 0.0));
    M                    = glm::scale(M, glm::vec3(0.3f));
    uniforms.modelMatrix = M;

    glm::mat4x4 V(1.0);
    V                   = glm::translate(V, -focalPoint);
    V                   = glm::rotate(V, -angle2, glm::vec3(1.0, 0.0, 0.0));
    uniforms.viewMatrix = V;

    float fov                 = 2 * glm::atan(1 / focalLength);
    uniforms.projectionMatrix = glm::perspective(fov, ratio, near, far);

    uniforms.time  = 1.0f;
    uniforms.color = {0.0f, 1.0f, 0.4f, 1.0f};

    data->S  = S;
    data->T1 = T1;

    return uniforms;
}
