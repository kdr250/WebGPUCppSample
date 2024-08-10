#include "Application.h"
#include "ResourceManager.h"

#include <GLFW/glfw3.h>
#include <glfw3webgpu.h>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_LEFT_HANDED
#include <glm/ext.hpp>
#include <glm/glm.hpp>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include "backends/imgui_impl_wgpu.h"

#include <array>
#include <cassert>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>

using namespace wgpu;
using VertexAttributes = ResourceManager::VertexAttributes;

constexpr float PI = 3.14159265358979323846f;

TextureView GetNextSurfaceTextureView(Surface surface);

///////////////////////////////////////////////////////////////////////////////
// Public methods

bool Application::onInit()
{
    if (!initWindowAndDevice())
        return false;
    if (!initSwapChain())
        return false;
    if (!initDepthBuffer())
        return false;
    if (!initRenderPipeline())
        return false;
    if (!initTexture())
        return false;
    if (!initGeometry())
        return false;
    if (!initUniforms())
        return false;
    if (!initBindGroup())
        return false;
    if (!initGui())
        return false;
    return true;
}

void Application::onFrame()
{
    updateDragInertia();

    glfwPollEvents();

    // Update uniform buffer
    m_uniforms.time = static_cast<float>(glfwGetTime());
    m_queue.writeBuffer(m_uniformBuffer, offsetof(MyUniforms, time), &m_uniforms.time, sizeof(MyUniforms::time));

    wgpu::TextureView nextTexture = GetNextSurfaceTextureView(m_surface);
    if (!nextTexture)
    {
        std::cerr << "Cannot acquire next swap chain texture" << std::endl;
        return;
    }

    CommandEncoderDescriptor commandEncoderDesc;
    commandEncoderDesc.label = "Command Encoder";
    CommandEncoder encoder   = m_device.createCommandEncoder(commandEncoderDesc);

    RenderPassDescriptor renderPassDesc {};

    RenderPassColorAttachment renderPassColorAttachment {};
    renderPassColorAttachment.view          = nextTexture;
    renderPassColorAttachment.resolveTarget = nullptr;
    renderPassColorAttachment.loadOp        = LoadOp::Clear;
    renderPassColorAttachment.storeOp       = StoreOp::Store;
    renderPassColorAttachment.clearValue    = Color {0.05, 0.05, 0.05, 1.0};
    renderPassDesc.colorAttachmentCount     = 1;
    renderPassDesc.colorAttachments         = &renderPassColorAttachment;

    RenderPassDepthStencilAttachment depthStencilAttachment;
    depthStencilAttachment.view              = m_depthTextureView;
    depthStencilAttachment.depthClearValue   = 1.0f;
    depthStencilAttachment.depthLoadOp       = LoadOp::Clear;
    depthStencilAttachment.depthStoreOp      = StoreOp::Store;
    depthStencilAttachment.depthReadOnly     = false;
    depthStencilAttachment.stencilClearValue = 0;
#ifdef WEBGPU_BACKEND_WGPU
    depthStencilAttachment.stencilLoadOp  = LoadOp::Clear;
    depthStencilAttachment.stencilStoreOp = StoreOp::Store;
#else
    depthStencilAttachment.stencilLoadOp  = LoadOp::Undefined;
    depthStencilAttachment.stencilStoreOp = StoreOp::Undefined;
#endif
    depthStencilAttachment.stencilReadOnly = true;

    renderPassDesc.depthStencilAttachment = &depthStencilAttachment;

    renderPassDesc.timestampWrites = nullptr;
    RenderPassEncoder renderPass   = encoder.beginRenderPass(renderPassDesc);

    renderPass.setPipeline(m_pipeline);

    renderPass.setVertexBuffer(0, m_vertexBuffer, 0, m_vertexCount * sizeof(VertexAttributes));

    // Set binding group
    renderPass.setBindGroup(0, m_bindGroup, 0, nullptr);

    renderPass.draw(m_vertexCount, 1, 0, 0);

    updateGui(renderPass);

    renderPass.end();
    renderPass.release();

    nextTexture.release();

    CommandBufferDescriptor cmdBufferDescriptor {};
    cmdBufferDescriptor.label = "Command buffer";
    CommandBuffer command     = encoder.finish(cmdBufferDescriptor);
    encoder.release();
    m_queue.submit(command);
    command.release();

#ifndef __EMSCRIPTEN__
    m_surface.present();
#endif

#if defined(WEBGPU_BACKEND_DAWN)
    m_device.tick();
#elif defined(WEBGPU_BACKEND_WGPU)
    m_device.poll(false);
#endif
}

void Application::onFinish()
{
    terminateGui();
    terminateBindGroup();
    terminateUniforms();
    terminateGeometry();
    terminateTexture();
    terminateRenderPipeline();
    terminateDepthBuffer();
    terminateWindowAndDevice();
}

bool Application::isRunning()
{
    return !glfwWindowShouldClose(m_window);
}

void Application::onResize()
{
    // Terminate in reverse order
    terminateDepthBuffer();

    // Re-init
    initSwapChain();
    initDepthBuffer();

    updateProjectionMatrix();
}

void Application::onMouseMove(double xpos, double ypos)
{
    if (!m_drag.active)
        return;

    vec2 currentMouse    = vec2(-(float)xpos, (float)ypos);
    vec2 delta           = (currentMouse - m_drag.startMouse) * m_drag.sensitivity;
    m_cameraState.angles = m_drag.startCameraState.angles + delta;
    // Clamp to avoid going too far when orbitting up/down
    m_cameraState.angles.y = glm::clamp(m_cameraState.angles.y, -PI / 2 + 1e-5f, PI / 2 - 1e-5f);
    updateViewMatrix();

    // Inertia
    m_drag.velocity      = delta - m_drag.previousDelta;
    m_drag.previousDelta = delta;
}

void Application::onMouseButton(int button, int action, int /* modifiers */)
{
    if (button == GLFW_MOUSE_BUTTON_LEFT)
    {
        switch (action)
        {
            case GLFW_PRESS:
                m_drag.active = true;
                double xpos, ypos;
                glfwGetCursorPos(m_window, &xpos, &ypos);
                m_drag.startMouse       = vec2(-(float)xpos, (float)ypos);
                m_drag.startCameraState = m_cameraState;
                break;
            case GLFW_RELEASE:
                m_drag.active = false;
                break;

            default:
                break;
        }
    }
}

void Application::onScroll(double /* xoffset */, double yoffset)
{
    m_cameraState.zoom += m_drag.scrollSensitivity * static_cast<float>(yoffset);
    m_cameraState.zoom = glm::clamp(m_cameraState.zoom, -2.0f, 2.0f);
    updateViewMatrix();
}

///////////////////////////////////////////////////////////////////////////////
// Private methods

bool Application::initWindowAndDevice()
{
    m_instance = createInstance(InstanceDescriptor {});
    if (!m_instance)
    {
        std::cerr << "Could not initialize WebGPU!" << std::endl;
        return false;
    }

    if (!glfwInit())
    {
        std::cerr << "Could not initialize GLFW!" << std::endl;
        return false;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    m_window = glfwCreateWindow(640, 480, "Learn WebGPU", NULL, NULL);
    if (!m_window)
    {
        std::cerr << "Could not open window!" << std::endl;
        return false;
    }

    std::cout << "Requesting adapter..." << std::endl;
    m_surface = glfwGetWGPUSurface(m_instance, m_window);
    RequestAdapterOptions adapterOpts {};
    adapterOpts.compatibleSurface = m_surface;
    Adapter adapter               = m_instance.requestAdapter(adapterOpts);
    std::cout << "Got adapter: " << adapter << std::endl;

    SupportedLimits supportedLimits;
    adapter.getLimits(&supportedLimits);

    std::cout << "Requesting device..." << std::endl;
    RequiredLimits requiredLimits                         = Default;
    requiredLimits.limits.maxVertexAttributes             = 4;
    requiredLimits.limits.maxVertexBuffers                = 1;
    requiredLimits.limits.maxBufferSize                   = 150000 * sizeof(VertexAttributes);
    requiredLimits.limits.maxVertexBufferArrayStride      = sizeof(VertexAttributes);
    requiredLimits.limits.minStorageBufferOffsetAlignment = supportedLimits.limits.minStorageBufferOffsetAlignment;
    requiredLimits.limits.minUniformBufferOffsetAlignment = supportedLimits.limits.minUniformBufferOffsetAlignment;
    requiredLimits.limits.maxInterStageShaderComponents   = 8;
    requiredLimits.limits.maxBindGroups                   = 2;
    requiredLimits.limits.maxUniformBuffersPerShaderStage = 1;
    requiredLimits.limits.maxUniformBufferBindingSize     = 16 * 4 * sizeof(float);
    // Allow textures up to 2K
    requiredLimits.limits.maxTextureDimension1D            = 2048;
    requiredLimits.limits.maxTextureDimension2D            = 2048;
    requiredLimits.limits.maxTextureArrayLayers            = 1;
    requiredLimits.limits.maxSampledTexturesPerShaderStage = 1;
    requiredLimits.limits.maxSamplersPerShaderStage        = 1;

    DeviceDescriptor deviceDesc;
    deviceDesc.label                = "My Device";
    deviceDesc.requiredFeatureCount = 0;
    deviceDesc.requiredLimits       = &requiredLimits;
    deviceDesc.defaultQueue.label   = "The default queue";
    m_device                        = adapter.requestDevice(deviceDesc);
    std::cout << "Got device: " << m_device << std::endl;

    // Add an error callback for more debug info
    m_errorCallbackHandle = m_device.setUncapturedErrorCallback(
        [](ErrorType type, char const* message)
        {
            std::cout << "Device error: type " << type;
            if (message)
                std::cout << " (message: " << message << ")";
            std::cout << std::endl;
        });

    m_queue = m_device.getQueue();

#ifdef WEBGPU_BACKEND_WGPU
    m_swapChainFormat = m_surface.getPreferredFormat(adapter);
#else
    m_swapChainFormat = TextureFormat::BGRA8Unorm;
#endif

    // Set the user pointer to be "this"
    glfwSetWindowUserPointer(m_window, this);
    // Add window callbacks
    glfwSetFramebufferSizeCallback(m_window,
                                   [](GLFWwindow* window, int, int)
                                   {
                                       auto that = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));
                                       if (that != nullptr)
                                           that->onResize();
                                   });
    glfwSetCursorPosCallback(m_window,
                             [](GLFWwindow* window, double xpos, double ypos)
                             {
                                 auto that = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));
                                 if (that != nullptr)
                                     that->onMouseMove(xpos, ypos);
                             });
    glfwSetMouseButtonCallback(m_window,
                               [](GLFWwindow* window, int button, int action, int mods)
                               {
                                   auto that = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));
                                   if (that != nullptr)
                                       that->onMouseButton(button, action, mods);
                               });
    glfwSetScrollCallback(m_window,
                          [](GLFWwindow* window, double xoffset, double yoffset)
                          {
                              auto that = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));
                              if (that != nullptr)
                                  that->onScroll(xoffset, yoffset);
                          });

    adapter.release();
    return m_device != nullptr;
}

void Application::terminateWindowAndDevice()
{
    m_queue.release();
    m_device.release();
    m_surface.release();
    m_instance.release();

    glfwDestroyWindow(m_window);
    glfwTerminate();
}

bool Application::initSwapChain()
{
    // get the current size of the window's framebuffer
    int width, height;
    glfwGetFramebufferSize(m_window, &width, &height);

    std::cout << "Creating swapchain..." << std::endl;
    SurfaceConfiguration config;
    config.width           = static_cast<uint32_t>(width);
    config.height          = static_cast<uint32_t>(height);
    config.usage           = TextureUsage::RenderAttachment;
    config.format          = m_swapChainFormat;
    config.viewFormatCount = 0;
    config.viewFormats     = nullptr;
    config.device          = m_device;
    config.presentMode     = PresentMode::Fifo;
    config.alphaMode       = CompositeAlphaMode::Auto;

    m_surface.configure(config);

    return true;
}

bool Application::initDepthBuffer()
{
    // get the current size of the window's framebuffer
    int width, height;
    glfwGetFramebufferSize(m_window, &width, &height);

    // Create the depth texture
    TextureDescriptor depthTextureDesc;
    depthTextureDesc.dimension       = TextureDimension::_2D;
    depthTextureDesc.format          = m_depthTextureFormat;
    depthTextureDesc.mipLevelCount   = 1;
    depthTextureDesc.sampleCount     = 1;
    depthTextureDesc.size            = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    depthTextureDesc.usage           = TextureUsage::RenderAttachment;
    depthTextureDesc.viewFormatCount = 1;
    depthTextureDesc.viewFormats     = (WGPUTextureFormat*)&m_depthTextureFormat;
    m_depthTexture                   = m_device.createTexture(depthTextureDesc);
    std::cout << "Depth texture: " << m_depthTexture << std::endl;

    // Create the view of the depth texture manipulated by the rasterizer
    TextureViewDescriptor depthTextureViewDesc;
    depthTextureViewDesc.aspect          = TextureAspect::DepthOnly;
    depthTextureViewDesc.baseArrayLayer  = 0;
    depthTextureViewDesc.arrayLayerCount = 1;
    depthTextureViewDesc.baseMipLevel    = 0;
    depthTextureViewDesc.mipLevelCount   = 1;
    depthTextureViewDesc.dimension       = TextureViewDimension::_2D;
    depthTextureViewDesc.format          = m_depthTextureFormat;
    m_depthTextureView                   = m_depthTexture.createView(depthTextureViewDesc);
    std::cout << "Depth texture view: " << m_depthTextureView << std::endl;

    return m_depthTextureView != nullptr;
}

void Application::terminateDepthBuffer()
{
    m_depthTextureView.release();
    m_depthTexture.destroy();
    m_depthTexture.release();
}

bool Application::initRenderPipeline()
{
    std::cout << "Creating shader module..." << std::endl;
    m_shaderModule = ResourceManager::loadShaderModule("resources/shader/sample.wgsl", m_device);
    std::cout << "Shader module: " << m_shaderModule << std::endl;

    std::cout << "Creating render pipeline..." << std::endl;
    RenderPipelineDescriptor pipelineDesc;

    // Vertex fetch
    std::vector<VertexAttribute> vertexAttribs(4);

    // Position attribute
    vertexAttribs[0].shaderLocation = 0;
    vertexAttribs[0].format         = VertexFormat::Float32x3;
    vertexAttribs[0].offset         = 0;

    // Normal attribute
    vertexAttribs[1].shaderLocation = 1;
    vertexAttribs[1].format         = VertexFormat::Float32x3;
    vertexAttribs[1].offset         = offsetof(VertexAttributes, normal);

    // Color attribute
    vertexAttribs[2].shaderLocation = 2;
    vertexAttribs[2].format         = VertexFormat::Float32x3;
    vertexAttribs[2].offset         = offsetof(VertexAttributes, color);

    // UV attribute
    vertexAttribs[3].shaderLocation = 3;
    vertexAttribs[3].format         = VertexFormat::Float32x2;
    vertexAttribs[3].offset         = offsetof(VertexAttributes, uv);

    VertexBufferLayout vertexBufferLayout;
    vertexBufferLayout.attributeCount = (uint32_t)vertexAttribs.size();
    vertexBufferLayout.attributes     = vertexAttribs.data();
    vertexBufferLayout.arrayStride    = sizeof(VertexAttributes);
    vertexBufferLayout.stepMode       = VertexStepMode::Vertex;

    pipelineDesc.vertex.bufferCount = 1;
    pipelineDesc.vertex.buffers     = &vertexBufferLayout;

    pipelineDesc.vertex.module        = m_shaderModule;
    pipelineDesc.vertex.entryPoint    = "vs_main";
    pipelineDesc.vertex.constantCount = 0;
    pipelineDesc.vertex.constants     = nullptr;

    pipelineDesc.primitive.topology         = PrimitiveTopology::TriangleList;
    pipelineDesc.primitive.stripIndexFormat = IndexFormat::Undefined;
    pipelineDesc.primitive.frontFace        = FrontFace::CCW;
    pipelineDesc.primitive.cullMode         = CullMode::None;

    FragmentState fragmentState;
    pipelineDesc.fragment       = &fragmentState;
    fragmentState.module        = m_shaderModule;
    fragmentState.entryPoint    = "fs_main";
    fragmentState.constantCount = 0;
    fragmentState.constants     = nullptr;

    BlendState blendState;
    blendState.color.srcFactor = BlendFactor::SrcAlpha;
    blendState.color.dstFactor = BlendFactor::OneMinusSrcAlpha;
    blendState.color.operation = BlendOperation::Add;
    blendState.alpha.srcFactor = BlendFactor::Zero;
    blendState.alpha.dstFactor = BlendFactor::One;
    blendState.alpha.operation = BlendOperation::Add;

    ColorTargetState colorTarget;
    colorTarget.format    = m_swapChainFormat;
    colorTarget.blend     = &blendState;
    colorTarget.writeMask = ColorWriteMask::All;

    fragmentState.targetCount = 1;
    fragmentState.targets     = &colorTarget;

    DepthStencilState depthStencilState = Default;
    depthStencilState.depthCompare      = CompareFunction::Less;
    depthStencilState.depthWriteEnabled = true;
    depthStencilState.format            = m_depthTextureFormat;
    depthStencilState.stencilReadMask   = 0;
    depthStencilState.stencilWriteMask  = 0;

    pipelineDesc.depthStencil = &depthStencilState;

    pipelineDesc.multisample.count                  = 1;
    pipelineDesc.multisample.mask                   = ~0u;
    pipelineDesc.multisample.alphaToCoverageEnabled = false;

    // Create binding layouts

    // Since we now have 2 bindings, we use a vector to store them
    std::vector<BindGroupLayoutEntry> bindingLayoutEntries(3, Default);

    // The uniform buffer binding that we already had
    BindGroupLayoutEntry& bindingLayout = bindingLayoutEntries[0];
    bindingLayout.binding               = 0;
    bindingLayout.visibility            = ShaderStage::Vertex | ShaderStage::Fragment;
    bindingLayout.buffer.type           = BufferBindingType::Uniform;
    bindingLayout.buffer.minBindingSize = sizeof(MyUniforms);

    // The texture binding
    BindGroupLayoutEntry& textureBindingLayout = bindingLayoutEntries[1];
    textureBindingLayout.binding               = 1;
    textureBindingLayout.visibility            = ShaderStage::Fragment;
    textureBindingLayout.texture.sampleType    = TextureSampleType::Float;
    textureBindingLayout.texture.viewDimension = TextureViewDimension::_2D;

    // The texture sampler binding
    BindGroupLayoutEntry& samplerBindingLayout = bindingLayoutEntries[2];
    samplerBindingLayout.binding               = 2;
    samplerBindingLayout.visibility            = ShaderStage::Fragment;
    samplerBindingLayout.sampler.type          = SamplerBindingType::Filtering;

    // Create a bind group layout
    BindGroupLayoutDescriptor bindGroupLayoutDesc {};
    bindGroupLayoutDesc.entryCount = (uint32_t)bindingLayoutEntries.size();
    bindGroupLayoutDesc.entries    = bindingLayoutEntries.data();
    m_bindGroupLayout              = m_device.createBindGroupLayout(bindGroupLayoutDesc);

    // Create the pipeline layout
    PipelineLayoutDescriptor layoutDesc {};
    layoutDesc.bindGroupLayoutCount = 1;
    layoutDesc.bindGroupLayouts     = (WGPUBindGroupLayout*)&m_bindGroupLayout;
    PipelineLayout layout           = m_device.createPipelineLayout(layoutDesc);
    pipelineDesc.layout             = layout;

    m_pipeline = m_device.createRenderPipeline(pipelineDesc);
    std::cout << "Render pipeline: " << m_pipeline << std::endl;

    return m_pipeline != nullptr;
}

void Application::terminateRenderPipeline()
{
    m_pipeline.release();
    m_shaderModule.release();
    m_bindGroupLayout.release();
}

bool Application::initTexture()
{
    // Create a sampler
    SamplerDescriptor samplerDesc;
    samplerDesc.addressModeU  = AddressMode::Repeat;
    samplerDesc.addressModeV  = AddressMode::Repeat;
    samplerDesc.addressModeW  = AddressMode::Repeat;
    samplerDesc.magFilter     = FilterMode::Linear;
    samplerDesc.minFilter     = FilterMode::Linear;
    samplerDesc.mipmapFilter  = MipmapFilterMode::Linear;
    samplerDesc.lodMinClamp   = 0.0f;
    samplerDesc.lodMaxClamp   = 8.0f;
    samplerDesc.compare       = CompareFunction::Undefined;
    samplerDesc.maxAnisotropy = 1;
    m_sampler                 = m_device.createSampler(samplerDesc);

    // Create a texture
    m_texture = ResourceManager::loadTexture("resources/shader/fourareen2K_albedo.jpg", m_device, &m_textureView);
    if (!m_texture)
    {
        std::cerr << "Could not load texture!" << std::endl;
        return false;
    }
    std::cout << "Texture: " << m_texture << std::endl;
    std::cout << "Texture view: " << m_textureView << std::endl;

    return m_textureView != nullptr;
}

void Application::terminateTexture()
{
    m_textureView.release();
    m_texture.destroy();
    m_texture.release();
    m_sampler.release();
}

bool Application::initGeometry()
{
    // Load mesh data from OBJ file
    std::vector<VertexAttributes> vertexData;
    bool success = ResourceManager::loadGeometryFromObj("resources/shader/fourareen.obj", vertexData);
    if (!success)
    {
        std::cerr << "Could not load geometry!" << std::endl;
        return false;
    }

    // Create vertex buffer
    BufferDescriptor bufferDesc;
    bufferDesc.size             = vertexData.size() * sizeof(VertexAttributes);
    bufferDesc.usage            = BufferUsage::CopyDst | BufferUsage::Vertex;
    bufferDesc.mappedAtCreation = false;
    m_vertexBuffer              = m_device.createBuffer(bufferDesc);
    m_queue.writeBuffer(m_vertexBuffer, 0, vertexData.data(), bufferDesc.size);

    m_vertexCount = static_cast<int>(vertexData.size());

    return m_vertexBuffer != nullptr;
}

void Application::terminateGeometry()
{
    m_vertexBuffer.destroy();
    m_vertexBuffer.release();
    m_vertexCount = 0;
}

bool Application::initUniforms()
{
    // Create uniform buffer
    BufferDescriptor bufferDesc;
    bufferDesc.size             = sizeof(MyUniforms);
    bufferDesc.usage            = BufferUsage::CopyDst | BufferUsage::Uniform;
    bufferDesc.mappedAtCreation = false;
    m_uniformBuffer             = m_device.createBuffer(bufferDesc);

    // Upload the initial value of the uniforms
    m_uniforms.modelMatrix      = mat4x4(1.0);
    m_uniforms.viewMatrix       = glm::lookAt(vec3(-2.0f, -3.0f, 2.0f), vec3(0.0f), vec3(0, 0, 1));
    m_uniforms.projectionMatrix = glm::perspective(45 * PI / 180, 640.0f / 480.0f, 0.01f, 100.0f);
    m_uniforms.time             = 1.0f;
    m_uniforms.color            = {0.0f, 1.0f, 0.4f, 1.0f};
    m_queue.writeBuffer(m_uniformBuffer, 0, &m_uniforms, sizeof(MyUniforms));

    updateViewMatrix();

    return m_uniformBuffer != nullptr;
}

void Application::terminateUniforms()
{
    m_uniformBuffer.destroy();
    m_uniformBuffer.release();
}

bool Application::initBindGroup()
{
    // Create a binding
    std::vector<BindGroupEntry> bindings(3);

    bindings[0].binding = 0;
    bindings[0].buffer  = m_uniformBuffer;
    bindings[0].offset  = 0;
    bindings[0].size    = sizeof(MyUniforms);

    bindings[1].binding     = 1;
    bindings[1].textureView = m_textureView;

    bindings[2].binding = 2;
    bindings[2].sampler = m_sampler;

    BindGroupDescriptor bindGroupDesc;
    bindGroupDesc.layout     = m_bindGroupLayout;
    bindGroupDesc.entryCount = (uint32_t)bindings.size();
    bindGroupDesc.entries    = bindings.data();
    m_bindGroup              = m_device.createBindGroup(bindGroupDesc);

    return m_bindGroup != nullptr;
}

void Application::terminateBindGroup()
{
    m_bindGroup.release();
}

void Application::updateProjectionMatrix()
{
    int width, height;
    glfwGetFramebufferSize(m_window, &width, &height);
    float ratio                 = width / (float)height;
    m_uniforms.projectionMatrix = glm::perspective(45 * PI / 180, ratio, 0.01f, 100.0f);
    m_queue.writeBuffer(m_uniformBuffer,
                        offsetof(MyUniforms, projectionMatrix),
                        &m_uniforms.projectionMatrix,
                        sizeof(MyUniforms::projectionMatrix));
}

void Application::updateViewMatrix()
{
    float cx              = cos(m_cameraState.angles.x);
    float sx              = sin(m_cameraState.angles.x);
    float cy              = cos(m_cameraState.angles.y);
    float sy              = sin(m_cameraState.angles.y);
    vec3 position         = vec3(cx * cy, sx * cy, sy) * std::exp(-m_cameraState.zoom);
    m_uniforms.viewMatrix = glm::lookAt(position, vec3(0.0f), vec3(0, 0, 1));
    m_queue.writeBuffer(m_uniformBuffer,
                        offsetof(MyUniforms, viewMatrix),
                        &m_uniforms.viewMatrix,
                        sizeof(MyUniforms::viewMatrix));
}

void Application::updateDragInertia()
{
    constexpr float eps = 1e-4f;

    if (m_drag.active)
        return;

    if (std::abs(m_drag.velocity.x) < eps && std::abs(m_drag.velocity.y) < eps)
        return;

    m_cameraState.angles += m_drag.velocity;
    m_cameraState.angles.y = glm::clamp(m_cameraState.angles.y, -PI / 2 + 1e-5f, PI / 2 - 1e-5f);
    m_drag.velocity *= m_drag.intertia;
    updateViewMatrix();
}

bool Application::initGui()
{
    // Setup ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO();

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOther(m_window, true);

    ImGui_ImplWGPU_InitInfo wgpuInfo;
    wgpuInfo.Device             = m_device;
    wgpuInfo.NumFramesInFlight  = 3;
    wgpuInfo.RenderTargetFormat = m_swapChainFormat;
    wgpuInfo.DepthStencilFormat = m_depthTextureFormat;
    ImGui_ImplWGPU_Init(&wgpuInfo);

    return true;
}

void Application::terminateGui()
{
    ImGui_ImplGlfw_Shutdown();
    ImGui_ImplWGPU_Shutdown();
}

void Application::updateGui(wgpu::RenderPassEncoder renderPass)
{
    // Start the ImGui frame
    ImGui_ImplWGPU_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Build our UI
    static float f                  = 0.0f;
    static int counter              = 0;
    static bool show_demo_window    = true;
    static bool show_another_window = false;
    static ImVec4 clear_color       = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    ImGui::Begin("Hello, world!");  // Create a window called "Hello, world!" and append into it.

    ImGui::Text("This is some useful text.");           // Display some text (you can use a format strings too)
    ImGui::Checkbox("Demo Window", &show_demo_window);  // Edit bools storing our window open/close state
    ImGui::Checkbox("Another Window", &show_another_window);

    ImGui::SliderFloat("float", &f, 0.0f, 1.0f);             // Edit 1 float using a slider from 0.0f to 1.0f
    ImGui::ColorEdit3("clear color", (float*)&clear_color);  // Edit 3 floats representing a color

    if (ImGui::Button("Button"))  // Buttons return true when clicked (most widgets return true when edited/activated)
        counter++;
    ImGui::SameLine();
    ImGui::Text("counter = %d", counter);

    ImGuiIO& io = ImGui::GetIO();
    ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
    ImGui::End();

    // Draw the UI
    ImGui::EndFrame();
    ImGui::Render();
    ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), renderPass);
}

TextureView GetNextSurfaceTextureView(Surface surface)
{
    SurfaceTexture surfaceTexture;
    surface.getCurrentTexture(&surfaceTexture);
    if (surfaceTexture.status != SurfaceGetCurrentTextureStatus::Success)
    {
        return nullptr;
    }
    Texture texture = surfaceTexture.texture;

    // Create a view for this surface texture
    TextureViewDescriptor viewDescriptor;
    viewDescriptor.label           = "Surface texture view";
    viewDescriptor.format          = texture.getFormat();
    viewDescriptor.dimension       = TextureViewDimension::_2D;
    viewDescriptor.baseMipLevel    = 0;
    viewDescriptor.mipLevelCount   = 1;
    viewDescriptor.baseArrayLayer  = 0;
    viewDescriptor.arrayLayerCount = 1;
    viewDescriptor.aspect          = TextureAspect::All;
    TextureView targetView         = texture.createView(viewDescriptor);

    return targetView;
}
