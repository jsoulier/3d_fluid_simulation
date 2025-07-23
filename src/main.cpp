#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <format>
#include <fstream>
#include <string>
#include <vector>

#include "config.hpp"
#include "debug_group.hpp"
#include "pipeline.hpp"
#include "rw_texture.hpp"
#include "shader.hpp"
#include "upload_buffer.hpp"

enum Texture
{
    TextureVelocityX,
    TextureVelocityY,
    TexturePressure,
    TextureDivergence,
    TextureDensity,
    TextureCount,
};

struct Cell
{
    int32_t x;
    int32_t y;
};

static_assert(TextureVelocityX == 0);
static_assert(TextureVelocityY == 1);

static constexpr int Width = 960;
static constexpr int Height = 720;

static SDL_Window* window;
static SDL_GPUDevice* device;
static SDL_GPUTexture* colorTexture;
static uint32_t width;
static uint32_t height;
static ReadWriteTexture textures[TextureCount];
static SDL_GPUSampler* sampler;
static float dt;
static uint64_t time1;
static uint64_t time2;
static int delay = 16;
static int cooldown;
static int size = 128;
static int iterations = 5;
static float diffusion = 0.01f;
static float viscosity = 0.01f;
static ComputeUploadBuffer<Cell> uploadBuffer;

static bool Init()
{
    SDL_SetAppMetadata("Fluid Simulation", nullptr, nullptr);
    SDL_SetLogPriorities(SDL_LOG_PRIORITY_VERBOSE);
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        SDL_Log("Failed to initialize SDL: %s", SDL_GetError());
        return false;
    }
    window = SDL_CreateWindow("Fluid Simulation", 960, 720, SDL_WINDOW_RESIZABLE);
    if (!window)
    {
        SDL_Log("Failed to create window: %s", SDL_GetError());
        return false;
    }
#if defined(SDL_PLATFORM_WIN32)
/* NOTE: forcing Vulkan when debugging on Windows since debug groups are broken on D3D12 */
#ifndef NDEBUG
    device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, true, nullptr);
#else
    device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_DXIL, true, nullptr);
#endif
#elif defined(SDL_PLATFORM_APPLE)
    device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_MSL, true, nullptr);
#else
    device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, true, nullptr);
#endif
    if (!device)
    {
        SDL_Log("Failed to create device: %s", SDL_GetError());
        return false;
    }
    if (!SDL_ClaimWindowForGPUDevice(device, window))
    {
        SDL_Log("Failed to create swapchain: %s", SDL_GetError());
        return false;
    }
    return true;
}

static bool CreateSamplers()
{
    SDL_GPUSamplerCreateInfo info{};
    info.min_filter = SDL_GPU_FILTER_NEAREST;
    info.mag_filter = SDL_GPU_FILTER_NEAREST;
    info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler = SDL_CreateGPUSampler(device, &info);
    if (!sampler)
    {
        SDL_Log("Failed to create sampler: %s", SDL_GetError());
        return false;
    }
    return true;
}

static bool CreateTextures()
{
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.width = Width;
    info.height = Height;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    info.format = SDL_GetGPUSwapchainTextureFormat(device, window);
    info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    colorTexture = SDL_CreateGPUTexture(device, &info);
    if (!colorTexture)
    {
        SDL_Log("Failed to create texture: %s", SDL_GetError());
        return false;
    }
    return true;
}

static void Add(SDL_GPUCommandBuffer* commandBuffer, Texture texture, float value)
{
    DEBUG_GROUP(device, commandBuffer);
    SDL_GPUComputePass* computePass = textures[texture].BeginReadPass(commandBuffer);
    if (!computePass)
    {
        SDL_Log("Failed to begin compute pass: %s", SDL_GetError());
        return;
    }
    int groups = (uploadBuffer.GetBufferSize() + THREADS - 1) / THREADS;
    SDL_GPUBuffer* buffer = uploadBuffer.GetBuffer();
    uint32_t bufferSize = uploadBuffer.GetBufferSize();
    BindPipeline(computePass, ComputePipelineTypeAdd);
    SDL_PushGPUComputeUniformData(commandBuffer, 0, &value, sizeof(value));
    SDL_BindGPUComputeStorageBuffers(computePass, 0, &buffer, 1);
    SDL_PushGPUComputeUniformData(commandBuffer, 0, &bufferSize, sizeof(bufferSize));
    SDL_PushGPUComputeUniformData(commandBuffer, 1, &value, sizeof(value));
    SDL_DispatchGPUCompute(computePass, groups, 1, 1);
    SDL_EndGPUComputePass(computePass);
}

static void Add(float x1, float y1, float x2, float y2)
{

}

static void Clear(SDL_GPUCommandBuffer* commandBuffer, ReadWriteTexture& texture, float value = 0.0f)
{
    DEBUG_GROUP(device, commandBuffer);
    SDL_GPUComputePass* computePass = texture.BeginWritePass(commandBuffer);
    if (!computePass)
    {
        SDL_Log("Failed to begin compute pass: %s", SDL_GetError());
        return;
    }
    int groups = (size + THREADS - 1) / THREADS;
    BindPipeline(computePass, ComputePipelineTypeClear);
    SDL_PushGPUComputeUniformData(commandBuffer, 0, &value, sizeof(value));
    SDL_DispatchGPUCompute(computePass, groups, groups, groups);
    SDL_EndGPUComputePass(computePass);
}

static bool CreateCells()
{
    SDL_GPUCommandBuffer* commandBuffer = SDL_AcquireGPUCommandBuffer(device);
    if (!commandBuffer)
    {
        SDL_Log("Failed to acquire command buffer: %s", SDL_GetError());
        return false;
    }
    for (int i = 0; i < TextureCount; i++)
    {
        if (!textures[i].Create(device, size))
        {
            SDL_Log("Failed to create texture: %d", i);
            return false;
        }
        Clear(commandBuffer, textures[i]);
        textures[i].Swap();
        Clear(commandBuffer, textures[i]);
    }
    SDL_SubmitGPUCommandBuffer(commandBuffer);
    return true;
}

static void Diffuse(SDL_GPUCommandBuffer* commandBuffer, ReadWriteTexture& texture, float diffusion)
{
    DEBUG_GROUP(device, commandBuffer);
    SDL_GPUComputePass* computePass = texture.BeginWritePass(commandBuffer);
    if (!computePass)
    {
        SDL_Log("Failed to begin compute pass: %s", SDL_GetError());
        return;
    }
    SDL_GPUTextureSamplerBinding textureBindings{};
    textureBindings.sampler = sampler;
    textureBindings.texture = texture.GetReadTexture();
    int groups = (size + THREADS - 1) / THREADS;
    BindPipeline(computePass, ComputePipelineTypeDiffuse);
    SDL_BindGPUComputeSamplers(computePass, 0, &textureBindings, 1);
    SDL_PushGPUComputeUniformData(commandBuffer, 0, &dt, sizeof(dt));
    SDL_PushGPUComputeUniformData(commandBuffer, 1, &diffusion, sizeof(diffusion));
    SDL_DispatchGPUCompute(computePass, groups, groups, 1);
    SDL_EndGPUComputePass(computePass);
    texture.Swap();
}

static void Project1(SDL_GPUCommandBuffer* commandBuffer)
{
    DEBUG_GROUP(device, commandBuffer);
    SDL_GPUStorageTextureReadWriteBinding readWriteTextureBindings[2]{};
    readWriteTextureBindings[0].texture = textures[TexturePressure].GetWriteTexture();
    readWriteTextureBindings[1].texture = textures[TextureDivergence].GetWriteTexture();
    SDL_GPUComputePass* computePass = SDL_BeginGPUComputePass(commandBuffer, readWriteTextureBindings, 2, nullptr, 0);
    if (!computePass)
    {
        SDL_Log("Failed to begin compute pass: %s", SDL_GetError());
        return;
    }
    SDL_GPUTextureSamplerBinding textureBindings[2]{};
    textureBindings[0].sampler = sampler;
    textureBindings[0].texture = textures[TextureVelocityX].GetReadTexture();
    textureBindings[1].sampler = sampler;
    textureBindings[1].texture = textures[TextureVelocityY].GetReadTexture();
    int groups = (size + THREADS - 1) / THREADS;
    BindPipeline(computePass, ComputePipelineTypeProject1);
    SDL_BindGPUComputeSamplers(computePass, 0, textureBindings, 2);
    SDL_DispatchGPUCompute(computePass, groups, groups, 1);
    SDL_EndGPUComputePass(computePass);
    textures[TexturePressure].Swap();
    textures[TextureDivergence].Swap();
}

static void Project2(SDL_GPUCommandBuffer* commandBuffer)
{
    DEBUG_GROUP(device, commandBuffer);
    SDL_GPUComputePass* computePass = textures[TexturePressure].BeginWritePass(commandBuffer);
    if (!computePass)
    {
        SDL_Log("Failed to begin compute pass: %s", SDL_GetError());
        return;
    }
    SDL_GPUTextureSamplerBinding textureBinding{};
    textureBinding.sampler = sampler;
    textureBinding.texture = textures[TextureDivergence].GetReadTexture();
    int groups = (size + THREADS - 1) / THREADS;
    BindPipeline(computePass, ComputePipelineTypeProject2);
    SDL_BindGPUComputeSamplers(computePass, 0, &textureBinding, 1);
    SDL_DispatchGPUCompute(computePass, groups, groups, 1);
    SDL_EndGPUComputePass(computePass);
    textures[TexturePressure].Swap();
}

static void Project3(SDL_GPUCommandBuffer* commandBuffer)
{
    DEBUG_GROUP(device, commandBuffer);
    SDL_GPUStorageTextureReadWriteBinding readWriteTextureBindings[2]{};
    readWriteTextureBindings[0].texture = textures[TextureVelocityX].GetWriteTexture();
    readWriteTextureBindings[1].texture = textures[TextureVelocityY].GetWriteTexture();
    SDL_GPUComputePass* computePass = SDL_BeginGPUComputePass(commandBuffer, readWriteTextureBindings, 2, nullptr, 0);
    if (!computePass)
    {
        SDL_Log("Failed to begin compute pass: %s", SDL_GetError());
        return;
    }
    SDL_GPUTextureSamplerBinding textureBindings[3]{};
    textureBindings[0].sampler = sampler;
    textureBindings[0].texture = textures[TexturePressure].GetReadTexture();
    textureBindings[1].sampler = sampler;
    textureBindings[1].texture = textures[TextureVelocityX].GetReadTexture();
    textureBindings[2].sampler = sampler;
    textureBindings[2].texture = textures[TextureVelocityY].GetReadTexture();
    int groups = (size + THREADS - 1) / THREADS;
    BindPipeline(computePass, ComputePipelineTypeProject3);
    SDL_BindGPUComputeSamplers(computePass, 0, textureBindings, 3);
    SDL_DispatchGPUCompute(computePass, groups, groups, 1);
    SDL_EndGPUComputePass(computePass);
    textures[TextureVelocityX].Swap();
    textures[TextureVelocityY].Swap();
}

static void Advect1(SDL_GPUCommandBuffer* commandBuffer, Texture texture)
{
    DEBUG_GROUP(device, commandBuffer);
    assert(texture == 0 || texture == 1 || texture == 2);
    SDL_GPUComputePass* computePass = textures[texture].BeginWritePass(commandBuffer);
    if (!computePass)
    {
        SDL_Log("Failed to begin compute pass: %s", SDL_GetError());
        return;
    }
    SDL_GPUTextureSamplerBinding textureBindings[2]{};
    textureBindings[0].sampler = sampler;
    textureBindings[0].texture = textures[TextureVelocityX].GetReadTexture();
    textureBindings[1].sampler = sampler;
    textureBindings[1].texture = textures[TextureVelocityY].GetReadTexture();
    int groups = (size + THREADS - 1) / THREADS;
    BindPipeline(computePass, ComputePipelineTypeAdvect1);
    SDL_BindGPUComputeSamplers(computePass, 0, textureBindings, 2);
    SDL_PushGPUComputeUniformData(commandBuffer, 0, &texture, sizeof(texture));
    SDL_PushGPUComputeUniformData(commandBuffer, 1, &dt, sizeof(dt));
    SDL_DispatchGPUCompute(computePass, groups, groups, 1);
    SDL_EndGPUComputePass(computePass);
}

static void Advect2(SDL_GPUCommandBuffer* commandBuffer)
{
    DEBUG_GROUP(device, commandBuffer);
    SDL_GPUComputePass* computePass = textures[TextureDensity].BeginWritePass(commandBuffer);
    if (!computePass)
    {
        SDL_Log("Failed to begin compute pass: %s", SDL_GetError());
        return;
    }
    SDL_GPUTextureSamplerBinding textureBindings[3]{};
    textureBindings[0].sampler = sampler;
    textureBindings[0].texture = textures[TextureDensity].GetReadTexture();
    textureBindings[1].sampler = sampler;
    textureBindings[1].texture = textures[TextureVelocityX].GetReadTexture();
    textureBindings[2].sampler = sampler;
    textureBindings[2].texture = textures[TextureVelocityY].GetReadTexture();
    int groups = (size + THREADS - 1) / THREADS;
    BindPipeline(computePass, ComputePipelineTypeAdvect2);
    SDL_BindGPUComputeSamplers(computePass, 0, textureBindings, 3);
    SDL_PushGPUComputeUniformData(commandBuffer, 0, &dt, sizeof(dt));
    SDL_DispatchGPUCompute(computePass, groups, groups, 1);
    SDL_EndGPUComputePass(computePass);
    textures[TextureDensity].Swap();
}

static void SetBnd2(SDL_GPUCommandBuffer* commandBuffer, ReadWriteTexture& texture, int type)
{
    DEBUG_GROUP(device, commandBuffer);
    SDL_GPUComputePass* computePass = texture.BeginWritePass(commandBuffer);
    if (!computePass)
    {
        SDL_Log("Failed to begin compute pass: %s", SDL_GetError());
        return;
    }
    SDL_GPUTextureSamplerBinding textureBindings{};
    textureBindings.sampler = sampler;
    textureBindings.texture = texture.GetReadTexture();
    int groups = (size + THREADS - 1) / THREADS;
    BindPipeline(computePass, ComputePipelineTypeSetBnd2);
    SDL_BindGPUComputeSamplers(computePass, 0, &textureBindings, 1);
    SDL_PushGPUComputeUniformData(commandBuffer, 0, &type, sizeof(type));
    SDL_DispatchGPUCompute(computePass, groups, 2, 1);
    SDL_EndGPUComputePass(computePass);
}

static void SetBnd3(SDL_GPUCommandBuffer* commandBuffer, ReadWriteTexture& texture, int type)
{
    DEBUG_GROUP(device, commandBuffer);
    SDL_GPUComputePass* computePass = texture.BeginWritePass(commandBuffer);
    if (!computePass)
    {
        SDL_Log("Failed to begin compute pass: %s", SDL_GetError());
        return;
    }
    SDL_GPUTextureSamplerBinding textureBindings{};
    textureBindings.sampler = sampler;
    textureBindings.texture = texture.GetReadTexture();
    int groups = (size + THREADS - 1) / THREADS;
    BindPipeline(computePass, ComputePipelineTypeSetBnd3);
    SDL_BindGPUComputeSamplers(computePass, 0, &textureBindings, 1);
    SDL_PushGPUComputeUniformData(commandBuffer, 0, &type, sizeof(type));
    SDL_DispatchGPUCompute(computePass, 2, groups, 1);
    SDL_EndGPUComputePass(computePass);
}

static void SetBnd4(SDL_GPUCommandBuffer* commandBuffer, ReadWriteTexture& texture)
{
    DEBUG_GROUP(device, commandBuffer);
    SDL_GPUComputePass* computePass = texture.BeginWritePass(commandBuffer);
    if (!computePass)
    {
        SDL_Log("Failed to begin compute pass: %s", SDL_GetError());
        return;
    }
    SDL_GPUTextureSamplerBinding textureBindings{};
    textureBindings.sampler = sampler;
    textureBindings.texture = texture.GetReadTexture();
    BindPipeline(computePass, ComputePipelineTypeSetBnd4);
    SDL_BindGPUComputeSamplers(computePass, 0, &textureBindings, 1);
    SDL_DispatchGPUCompute(computePass, 1, 1, 1);
    SDL_EndGPUComputePass(computePass);
}

static void SetBnd5(SDL_GPUCommandBuffer* commandBuffer, ReadWriteTexture& texture)
{
    DEBUG_GROUP(device, commandBuffer);
    SDL_GPUComputePass* computePass = texture.BeginWritePass(commandBuffer);
    if (!computePass)
    {
        SDL_Log("Failed to begin compute pass: %s", SDL_GetError());
        return;
    }
    SDL_GPUTextureSamplerBinding textureBindings{};
    textureBindings.sampler = sampler;
    textureBindings.texture = texture.GetReadTexture();
    int groups = (size + THREADS - 1) / THREADS;
    BindPipeline(computePass, ComputePipelineTypeSetBnd5);
    SDL_BindGPUComputeSamplers(computePass, 0, &textureBindings, 1);
    SDL_DispatchGPUCompute(computePass, groups, groups, 1);
    SDL_EndGPUComputePass(computePass);
}

static void SetBnd(SDL_GPUCommandBuffer* commandBuffer, ReadWriteTexture& texture, int type)
{
    /* TODO: only sides and corners are handled in the example (not edges) */
    // SetBnd1(commandBuffer, texture, type);
    // SetBnd2(commandBuffer, texture, type);
    // SetBnd3(commandBuffer, texture, type);
    // SetBnd4(commandBuffer, texture);
    // SetBnd5(commandBuffer, texture);
    // texture.Swap();
}

static void RenderCombined(SDL_GPUCommandBuffer* commandBuffer)
{
    DEBUG_GROUP(device, commandBuffer);
    SDL_GPUColorTargetInfo colorInfo{};
    colorInfo.texture = colorTexture;
    colorInfo.load_op = SDL_GPU_LOADOP_CLEAR;
    colorInfo.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass* renderPass = SDL_BeginGPURenderPass(commandBuffer, &colorInfo, 1, nullptr);
    if (!renderPass)
    {
        SDL_Log("Failed to begin render pass: %s", SDL_GetError());
        return;
    }
    SDL_GPUTextureSamplerBinding textureBindings[3]{};
    textureBindings[0].sampler = sampler;
    textureBindings[0].texture = textures[TextureVelocityX].GetReadTexture();
    textureBindings[1].sampler = sampler;
    textureBindings[1].texture = textures[TextureVelocityY].GetReadTexture();
    textureBindings[2].sampler = sampler;
    textureBindings[2].texture = textures[TextureDensity].GetReadTexture();
    BindPipeline(renderPass, GraphicsPipelineTypeCombined);
    SDL_BindGPUFragmentSamplers(renderPass, 0, textureBindings, 3);
    SDL_DrawGPUPrimitives(renderPass, 4, 1, 0, 0);
    SDL_EndGPURenderPass(renderPass);
}

static void Letterbox(SDL_GPUCommandBuffer* commandBuffer, SDL_GPUTexture* swapchainTexture)
{
    DEBUG_GROUP(device, commandBuffer);
    SDL_GPUBlitInfo info{};
    const float colorRatio = static_cast<float>(Width) / Height;
    const float swapchainRatio = static_cast<float>(width) / height;
    float scale = 0.0f;
    float letterboxW = 0.0f;
    float letterboxH = 0.0f;
    float letterboxX = 0.0f;
    float letterboxY = 0.0f;
    if (colorRatio > swapchainRatio)
    {
        scale = static_cast<float>(width) / Width;
        letterboxW = width;
        letterboxH = Height * scale;
        letterboxX = 0.0f;
        letterboxY = (height - letterboxH) / 2.0f;
    }
    else
    {
        scale = static_cast<float>(height) / Height;
        letterboxH = height;
        letterboxW = Width * scale;
        letterboxX = (width - letterboxW) / 2.0f;
        letterboxY = 0.0f;
    }
    SDL_FColor clearColor = {0.02f, 0.02f, 0.02f, 1.0f};
    info.load_op = SDL_GPU_LOADOP_CLEAR;
    info.clear_color = clearColor;
    info.source.texture = colorTexture;
    info.source.w = Width;
    info.source.h = Height;
    info.destination.texture = swapchainTexture;
    info.destination.x = letterboxX;
    info.destination.y = letterboxY;
    info.destination.w = letterboxW;
    info.destination.h = letterboxH;
    info.filter = SDL_GPU_FILTER_NEAREST;
    SDL_BlitGPUTexture(commandBuffer, &info);
}

static void Update()
{
    SDL_GPUCommandBuffer* commandBuffer = SDL_AcquireGPUCommandBuffer(device);
    if (!commandBuffer)
    {
        SDL_Log("Failed to acquire command buffer: %s", SDL_GetError());
        return;
    }
    SDL_GPUTexture* swapchainTexture;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(commandBuffer, window, &swapchainTexture, &width, &height))
    {
        SDL_Log("Failed to acquire swapchain texture: %s", SDL_GetError());
        SDL_CancelGPUCommandBuffer(commandBuffer);
        return;
    }
    if (!swapchainTexture || !width || !height)
    {
        /* NOTE: not an error */
        SDL_CancelGPUCommandBuffer(commandBuffer);
        return;
    }
    if (cooldown <= 0)
    {
        for (int i = 0; i < iterations; i++)
        {
            Diffuse(commandBuffer, textures[TextureVelocityX], viscosity);
            Diffuse(commandBuffer, textures[TextureVelocityY], viscosity);
            SetBnd(commandBuffer, textures[TextureVelocityX], 1);
            SetBnd(commandBuffer, textures[TextureVelocityY], 2);
        }
        Project1(commandBuffer);
        SetBnd(commandBuffer, textures[TextureDivergence], 0);
        SetBnd(commandBuffer, textures[TexturePressure], 0);
        for (int i = 0; i < iterations; i++)
        {
            Project2(commandBuffer);
            SetBnd(commandBuffer, textures[TexturePressure], 0);
        }
        Project3(commandBuffer);
        SetBnd(commandBuffer, textures[TextureVelocityX], 1);
        SetBnd(commandBuffer, textures[TextureVelocityY], 2);
        Advect1(commandBuffer, TextureVelocityX);
        Advect1(commandBuffer, TextureVelocityY);
        textures[TextureVelocityX].Swap();
        textures[TextureVelocityY].Swap();
        SetBnd(commandBuffer, textures[TextureVelocityX], 1);
        SetBnd(commandBuffer, textures[TextureVelocityY], 2);
        Project1(commandBuffer);
        Project2(commandBuffer);
        Project3(commandBuffer);
        Diffuse(commandBuffer, textures[TextureDensity], diffusion);
        Advect2(commandBuffer);
        SetBnd(commandBuffer, textures[TextureDensity], 0);
        cooldown = delay;
    }
    RenderCombined(commandBuffer);
    Letterbox(commandBuffer, swapchainTexture);
    SDL_SubmitGPUCommandBuffer(commandBuffer);
    SDL_WaitForGPUIdle(device);
}

int main(int argc, char** argv)
{
    if (!Init())
    {
        SDL_Log("Failed to initialize");
        return 1;
    }
    if (!CreatePipelines(device, window))
    {
        SDL_Log("Failed to create pipelines");
        return 1;
    }
    if (!CreateSamplers())
    {
        SDL_Log("Failed to create samplers");
        return 1;
    }
    if (!CreateTextures())
    {
        SDL_Log("Failed to create textures");
        return 1;
    }
    if (!CreateCells())
    {
        SDL_Log("Failed to create cells");
        return 1;
    }
    bool running = true;
    while (running)
    {
        time2 = SDL_GetTicks();
        dt = time2 - time1;
        cooldown -= dt;
        time1 = time2;
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            switch (event.type)
            {
            case SDL_EVENT_KEY_DOWN:
                if (event.key.scancode == SDL_SCANCODE_R)
                {
                    CreateCells();
                }
                break;
            case SDL_EVENT_MOUSE_MOTION:
                float x1 = event.motion.x - event.motion.xrel;
                float y1 = event.motion.y - event.motion.yrel;
                float x2 = event.motion.x;
                float y2 = event.motion.y;
                Add(x1, y1, x2, y2);
                break;
            case SDL_EVENT_QUIT:
                running = false;
                break;
            }
        }
        if (!running)
        {
            break;
        }
        float x;
        float y;
        SDL_GetMouseState(&x, &y);
        Add(x, y, x, y);
        Update();
    }
    SDL_HideWindow(window);
    for (int i = 0; i < TextureCount; i++)
    {
        textures[i].Free(device);
    }
    SDL_ReleaseGPUTexture(device, colorTexture);
    SDL_ReleaseGPUSampler(device, sampler);
    FreePipelines(device);
    SDL_ReleaseWindowFromGPUDevice(device, window);
    SDL_DestroyGPUDevice(device);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}