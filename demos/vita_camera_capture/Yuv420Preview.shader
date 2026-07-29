shader_type canvas_item;
render_mode unshaded;

uniform sampler2D cbcr_texture;

void fragment() {
	float y = texture(TEXTURE, UV).r;
	vec2 cbcr = texture(cbcr_texture, UV).rg - vec2(0.5, 0.5);
	vec3 rgb = mat3(
			vec3(1.0, 1.0, 1.0),
			vec3(0.0, -0.34413, 1.772),
			vec3(1.402, -0.71414, 0.0)) *
			vec3(y, cbcr);
	COLOR = vec4(rgb, 1.0);
}
