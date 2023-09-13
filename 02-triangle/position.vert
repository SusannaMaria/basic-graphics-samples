#version 450

out gl_PerVertex {
    vec4 gl_Position;
};

void main()
{
    vec2 positions[3] = vec2[](
        vec2( 0.0,-0.3),
        vec2(-0.6, 0.3),
        vec2( 0.6, 0.3)
    );

    gl_Position = vec4(positions[gl_VertexIndex], 0., 1.);
}
