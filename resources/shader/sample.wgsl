/**
 * Variable in the *uniform* address space
 * The memory location of the uniform is given by a pair of a *bind group* and
 * a *binding*.
 */
@group(0) @binding(0) var<uniform> uTime; f32;


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
    let ratio = 640.0 / 480.0;
    // We move the scene depending on the time
    let offset = vec2f(-0.6875, -0.463);
    offset += 0.3 * vec2f(cos(uTime), sin(uTime));
    out.position = vec4f(in.position.x + offset.x, (in.position.y + offset.y) * ratio, 0.0, 1.0);
    out.color = in.color;
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f
{
    return vec4f(in.color, 1.0);
}
