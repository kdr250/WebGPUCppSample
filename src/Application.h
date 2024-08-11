#pragma once

#include <array>
#include <glm/glm.hpp>
#include <webgpu/webgpu.hpp>

// Forward declare
struct GLFWwindow;

class Application
{
public:
    // A function called only once at the beginning. Returns false is init failed.
    bool onInit();

    // A function called at each frame, guaranteed never to be called before `onInit`.
    void onFrame();

    // A function called only once at the very end.
    void onFinish();

    // A function that tells if the application is still running.
    bool isRunning();

    // A function called when the window is resized
    void onResize();

    // Mouse events
    void onMouseMove(double xpos, double ypos);
    void onMouseButton(int button, int action, int mods);
    void onScroll(double xoffset, double yoffset);

private:
    bool initWindowAndDevice();
    void terminateWindowAndDevice();

    bool initSwapChain();

    bool initDepthBuffer();
    void terminateDepthBuffer();

    bool initRenderPipeline();
    void terminateRenderPipeline();

    bool initTexture();
    void terminateTexture();

    bool initGeometry();
    void terminateGeometry();

    bool initUniforms();
    void terminateUniforms();

    bool initBindGroupLayout();
    void terminateBindGroupLayout();

    bool initBindGroup();
    void terminateBindGroup();

    void updateProjectionMatrix();
    void updateViewMatrix();

    void updateDragInertia();

    bool initGui();                                      // called in onInit
    void terminateGui();                                 // called in onFinish
    void updateGui(wgpu::RenderPassEncoder renderPass);  // called in onFrame

    bool initLightingUniforms();       // called in onInit()
    void terminateLightingUniforms();  // called in onFinish()
    void updateLightingUniforms();     // called when GUI is tweaked

private:
    // (Just aliases to make notations lighter)
    using mat4x4 = glm::mat4x4;
    using vec4   = glm::vec4;
    using vec3   = glm::vec3;
    using vec2   = glm::vec2;

    /**
	 * The same structure as in the shader, replicated in C++
	 */
    struct MyUniforms
    {
        // We add transform matrices
        mat4x4 projectionMatrix;
        mat4x4 viewMatrix;
        mat4x4 modelMatrix;
        vec4 color;
        vec3 cameraWorldPosition;
        float time;
    };
    // Have the compiler check byte alignment
    static_assert(sizeof(MyUniforms) % 16 == 0);

    struct LightingUniforms
    {
        std::array<vec4, 2> directions;
        std::array<vec4, 2> colors;
        float hardness;
        float kd;
        float ks;

        float _pad[1];
    };
    static_assert(sizeof(LightingUniforms) % 16 == 0);

    struct CameraState
    {
        vec2 angles = {0.8f, 0.5f};
        float zoom  = -1.2f;
    };

    struct DragState
    {
        bool active = false;
        vec2 startMouse;
        CameraState startCameraState;

        float sensitivity       = 0.01f;
        float scrollSensitivity = 0.1f;

        vec2 velocity = {0.0, 0.0};
        vec2 previousDelta;
        float intertia = 0.9f;
    };

    // Window and Device
    GLFWwindow* m_window                  = nullptr;
    wgpu::Instance m_instance             = nullptr;
    wgpu::Surface m_surface               = nullptr;
    wgpu::Device m_device                 = nullptr;
    wgpu::Queue m_queue                   = nullptr;
    wgpu::TextureFormat m_swapChainFormat = wgpu::TextureFormat::Undefined;
    // Keep the error callback alive
    std::unique_ptr<wgpu::ErrorCallback> m_errorCallbackHandle;

    // Depth Buffer
    wgpu::TextureFormat m_depthTextureFormat = wgpu::TextureFormat::Depth24Plus;
    wgpu::Texture m_depthTexture             = nullptr;
    wgpu::TextureView m_depthTextureView     = nullptr;

    // Render Pipeline
    wgpu::BindGroupLayout m_bindGroupLayout = nullptr;
    wgpu::ShaderModule m_shaderModule       = nullptr;
    wgpu::RenderPipeline m_pipeline         = nullptr;

    // Texture
    wgpu::Sampler m_sampler         = nullptr;
    wgpu::Texture m_texture         = nullptr;
    wgpu::TextureView m_textureView = nullptr;

    // Geometry
    wgpu::Buffer m_vertexBuffer = nullptr;
    int m_vertexCount           = 0;

    // Uniforms
    wgpu::Buffer m_uniformBuffer = nullptr;
    MyUniforms m_uniforms;

    // Lighting
    wgpu::Buffer m_lightingUniformBuffer = nullptr;
    LightingUniforms m_lightingUniforms;
    bool m_lightingUniformsChanged = true;

    // Bind Group
    wgpu::BindGroup m_bindGroup = nullptr;

    CameraState m_cameraState;
    DragState m_drag;
};
