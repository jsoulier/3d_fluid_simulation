#ifndef SHADER_GLSL
#define SHADER_GLSL

/* modified to only read from the read image */
#define LIN_SOLVE(id, inImage, outImage, a, c) \
    do \
    { \
        imageStore(outImage, id, vec4( \
            (texelFetch(inImage, id, 0).x + \
                a * (texelFetch(inImage, id + ivec2( 1, 0 ), 0).x + \
                     texelFetch(inImage, id + ivec2(-1, 0 ), 0).x + \
                     texelFetch(inImage, id + ivec2( 0, 1 ), 0).x + \
                     texelFetch(inImage, id + ivec2( 0,-1 ), 0).x)) / c)); \
    } \
    while (false) \

#define ADVECT(id, outImage, inImage, inVelocityX, inVelocityY, deltaTime, size) \
    do \
    { \
        float N = size.x - 2; \
        float dtx = deltaTime * N; \
        float dty = deltaTime * N; \
        float tmp1 = dtx * texelFetch(inVelocityX, id, 0).x; \
        float tmp2 = dty * texelFetch(inVelocityY, id, 0).x; \
        float x = clamp(id.x - tmp1, 0.5f, N + 0.5f); \
        float y = clamp(id.y - tmp2, 0.5f, N + 0.5f); \
        float i0 = floor(x); \
        float i1 = i0 + 1.0f; \
        float j0 = floor(y); \
        float j1 = j0 + 1.0f; \
        float s1 = x - i0; \
        float s0 = 1.0f - s1; \
        float t1 = y - j0; \
        float t0 = 1.0f - t1; \
        int i0i = int(i0); \
        int i1i = int(i1); \
        int j0i = int(j0); \
        int j1i = int(j1); \
        imageStore(outImage, id, vec4( \
            s0 * (t0 * texelFetch(inImage, ivec2(i0i, j0i), 0).x + \
                  t1 * texelFetch(inImage, ivec2(i0i, j1i), 0).x) + \
            s1 * (t0 * texelFetch(inImage, ivec2(i1i, j0i), 0).x + \
                  t1 * texelFetch(inImage, ivec2(i1i, j1i), 0).x))); \
    } \
    while (false) \

#endif