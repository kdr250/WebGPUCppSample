/**
 * A structure with fields labeled with vertex attribute locations can be used
 * as input to the entry point of a shader.
 */
struct VertexInput
{
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
	@location(2) color: vec3f,
	@location(3) uv: vec2f,
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
	@location(2) uv: vec2f,
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
@group(0) @binding(1) var baseColorTexture: texture_2d<f32>;
@group(0) @binding(2) var textureSampler: sampler;

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
	out.uv = in.uv;
	return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f
{
	// Compute shading
	let normal = normalize(in.normal);
	let lightDirection1 = vec3f(0.5, -0.9, 0.1);
	let lightDirection2 = vec3f(0.2, 0.4, 0.3);
	let lightColor1 = vec3f(1.0, 0.9, 0.6);
	let lightColor2 = vec3f(0.6, 0.9, 1.0);
	let shading1 = max(0.0, dot(lightDirection1, normal));
	let shading2 = max(0.0, dot(lightDirection2, normal));
	let shading = shading1 * lightColor1 + shading2 * lightColor2;

	// Sample texture
	let baseColor = textureSample(baseColorTexture, textureSampler, in.uv).rgb;

	// Combine texture and lighting
	let color = baseColor * shading;

    // Gamma-correction
    let corrected_color = pow(color, vec3f(2.2));
    return vec4f(corrected_color, uMyUniforms.color.a);
}
