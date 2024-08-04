/**
 * A structure with fields labeled with vertex attribute locations can be used
 * as input to the entry point of a shader.
 */
struct VertexInput
{
    @location(0) position: vec2f,
    @location(1) color: vec3f,
};

/**
 * A structure with fields labeled with builtins and locations can also be used
 * as *output* of the vertex shader, which is also the input of the fragment
 * shader.
 */
struct VertexOutput
{
    @builtin(position) position: vec4f,
    @location(0) color: vec3f
};

@vertex
fn vs_main(in: VertexInput) -> VertexOutput
{
    var out: VertexOutput;
    out.position = vec4f(in.position, 0.0, 1.0);
    out.color = in.color;
    return vec4f(in_vertex_position, 0.0, 1.0);
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f
{
    return vec4f(in.color, 1.0);
}
