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
	@location(3) viewDirection: vec3f,
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
	cameraWorldPosition: vec3f,
    time: f32,
};

struct LightingUniforms
{
	directions: array<vec4f, 2>,
	colors: array<vec4f, 2>,
	hardness: f32,
	kd: f32,
	ks: f32,
};

@group(0) @binding(0) var<uniform> uMyUniforms: MyUniforms;
@group(0) @binding(1) var baseColorTexture: texture_2d<f32>;
@group(0) @binding(2) var textureSampler: sampler;
@group(0) @binding(3) var<uniform> uLighting: LightingUniforms;

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

	let worldPosition = uMyUniforms.modelMatrix * vec4<f32>(in.position, 1.0);
	out.position = uMyUniforms.projectionMatrix * uMyUniforms.viewMatrix * worldPosition;
	
	// Forward the normal
	out.normal = (uMyUniforms.modelMatrix * vec4f(in.normal, 0.0)).xyz;
	out.color = in.color;
	out.uv = in.uv;

	// Then we only need the camera position to get the view direction
	out.viewDirection = uMyUniforms.cameraWorldPosition - worldPosition.xyz;
	return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f
{
	// Compute shading
	let N = normalize(in.normal);
	let V = normalize(in.viewDirection);
	
	// Sample texture
	let baseColor = textureSample(baseColorTexture, textureSampler, in.uv).rgb;
	let kd = uLighting.kd; // strength of the diffuse effect
	let ks = uLighting.ks; // strength of the specular effect
	let hardness = uLighting.hardness;

	var color = vec3f(0.0);
	for (var i : i32 = 0; i < 2; i++)
	{
		let lightColor = uLighting.colors[i].rgb;
		let L = normalize(uLighting.directions[i].xyz);
		let R = reflect(-L, N);

		let diffuse = max(0.0, dot(L, N)) * lightColor;

		// We clamp the dot product to 0 when it is negative
		let RoV = max(0.0, dot(R, V));
		let specular = pow(RoV, hardness);

		color += baseColor * kd * diffuse + ks * specular;
	}

    // Gamma-correction
    let corrected_color = pow(color, vec3f(2.2));
    return vec4f(corrected_color, uMyUniforms.color.a);
}
