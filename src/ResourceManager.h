#pragma once

#include <glm/glm.hpp>
#include <webgpu/webgpu.hpp>

#include <filesystem>
#include <vector>

class ResourceManager
{
public:
    // (Just aliases to make notations lighter)
    using path = std::filesystem::path;
    using vec3 = glm::vec3;
    using vec2 = glm::vec2;

    /**
	 * A structure that describes the data layout in the vertex buffer,
	 * used by loadGeometryFromObj and used it in `sizeof` and `offsetof`
	 * when uploading data to the GPU.
	 */
    struct VertexAttributes
    {
        vec3 position;
        // Texture mapping attributes represent the local frame in which
        // normals sampled from the normal map are expressed.
        vec3 tangent;    // T = local X axis
        vec3 bitangent;  // B = local Y axis
        vec3 normal;     // N = local Z axis
        vec3 color;
        vec2 uv;
    };

    // Load a shader from a WGSL file into a new shader module
    static wgpu::ShaderModule loadShaderModule(const path& path, wgpu::Device device);

    // Load an 3D mesh from a standard .obj file into a vertex data buffer
    static bool loadGeometryFromObj(const path& path, std::vector<VertexAttributes>& vertexData);

    // Load an image from a standard image file into a new texture object
    // NB: The texture must be destroyed after use
    static wgpu::Texture loadTexture(const path& path, wgpu::Device device, wgpu::TextureView* pTextureView = nullptr);

private:
    // Compute the TBN local to a triangle face from its corners and return it as
    // a matrix whose columns are the T, B and N vectors.
    static glm::mat3x3 computeTBN(const VertexAttributes corners[3], const vec3& expectedN);

    // Compute Tangent and Bitangent attributes from the normal and UVs.
    static void populateTextureFrameAttributes(std::vector<VertexAttributes>& vertexData);
};
