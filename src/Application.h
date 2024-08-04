#pragma once

namespace wgpu
{
    class Adapter;
    class TextureView;
    struct RequiredLimits;
}  // namespace wgpu

class Application
{
public:
    Application();
    ~Application();

    // Initialize everything and return true if it went all right
    bool Initialize();

    // Uninitialize everything that was initialized
    void Terminate();

    // Draw a frame and handle events
    void MainLoop();

    // Return true as long as the main loop should keep on running
    bool IsRunning();

private:
    struct AppData;

    AppData* data;

    wgpu::TextureView GetNextSurfaceTextureView();

    void InitializePipeline();

    void InitializeBuffers();

    wgpu::RequiredLimits GetRequiredLimits(wgpu::Adapter adapter) const;
};
