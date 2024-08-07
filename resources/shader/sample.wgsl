/**
 * A structure with fields labeled with vertex attribute locations can be used
 * as input to the entry point of a shader.
 */
struct VertexInput
{
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
	@location(2) color: vec3f,
};

/**
 * A structure with fields labeled with builtins and locations can also be used
 * as *output* of the vertex shader, which is also the input of the fragment
 * shader.
 */
struct VertexOutput
{
    @builtin(position) position: vec4f,
    @location(0) color: vec3f,
	@location(1) normal: vec3f,
};

/**
 * A structure holding the value of our uniforms
 */
struct MyUniforms
{
	projectionMatrix: mat4x4f,
	viewMatrix: mat4x4f,
	modelMatrix: mat4x4f,
    color: vec4f,
    time: f32,
};

@group(0) @binding(0) var<uniform> uMyUniforms: MyUniforms;
@group(0) @binding(1) var gradientTexture: texture_2d<f32>;

const pi = 3.14159265359;

// Build an orthographic projection matrix
fn makeOrthographicProj(ratio: f32, near: f32, far: f32, scale: f32) -> mat4x4f {
	return transpose(mat4x4f(
		1.0 / scale,      0.0,           0.0,                  0.0,
		    0.0,     ratio / scale,      0.0,                  0.0,
		    0.0,          0.0,      1.0 / (far - near), -near / (far - near),
		    0.0,          0.0,           0.0,                  1.0,
	));
}

// Build a perspective projection matrix
fn makePerspectiveProj(ratio: f32, near: f32, far: f32, focalLength: f32) -> mat4x4f {
	let divides = 1.0 / (far - near);
	return transpose(mat4x4f(
		focalLength,         0.0,              0.0,               0.0,
		    0.0,     focalLength * ratio,      0.0,               0.0,
		    0.0,             0.0,         far * divides, -far * near * divides,
		    0.0,             0.0,              1.0,               0.0,
	));
}

@vertex
fn vs_main(in: VertexInput) -> VertexOutput
{
    var out: VertexOutput;
	out.position = uMyUniforms.projectionMatrix * uMyUniforms.viewMatrix * uMyUniforms.modelMatrix * vec4f(in.position, 1.0);
	// Forward the normal
	out.normal = (uMyUniforms.modelMatrix * vec4f(in.normal, 0.0)).xyz;
	out.color = in.color;
	return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f
{
	let color = textureLoad(gradientTexture, vec2<i32>(in.position.xy), 0).rgb;
    return vec4f(color, uMyUniforms.color.a);
}
