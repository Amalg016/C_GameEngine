#include "vulkan_pipeline.h"

#include <stdio.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------
// Shader loading
// ---------------------------------------------------------------------------

static uint32_t *read_spirv(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (f == nullptr) {
        fprintf(stderr, "[vulkan] cannot open shader: %s\n", path);
        return nullptr;
    }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (len <= 0 || len % 4 != 0) {
        fprintf(stderr, "[vulkan] invalid SPIR-V file: %s\n", path);
        fclose(f);
        return nullptr;
    }

    uint32_t *buf = malloc((size_t)len);
    if (buf == nullptr) { fclose(f); return nullptr; }

    fread(buf, 1, (size_t)len, f);
    fclose(f);

    *out_size = (size_t)len;
    return buf;
}

static VkShaderModule create_shader_module(VkDevice device,
                                           const uint32_t *code,
                                           size_t size) {
    VkShaderModuleCreateInfo ci = {
        .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = size,
        .pCode    = code,
    };

    VkShaderModule module;
    if (vkCreateShaderModule(device, &ci, nullptr, &module) != VK_SUCCESS) {
        fprintf(stderr, "[vulkan] failed to create shader module\n");
        return VK_NULL_HANDLE;
    }
    return module;
}

// ---------------------------------------------------------------------------
// Render pass
// ---------------------------------------------------------------------------

static bool create_render_pass(VulkanContext *ctx) {
    VkAttachmentDescription color_att = {
        .format         = ctx->swapchain.image_format,
        .samples        = VK_SAMPLE_COUNT_1_BIT,
        .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    };

    VkAttachmentReference color_ref = {
        .attachment = 0,
        .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };

    VkSubpassDescription subpass = {
        .pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments    = &color_ref,
    };

    VkSubpassDependency dep = {
        .srcSubpass    = VK_SUBPASS_EXTERNAL,
        .dstSubpass    = 0,
        .srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = 0,
        .dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
    };

    VkRenderPassCreateInfo ci = {
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments    = &color_att,
        .subpassCount    = 1,
        .pSubpasses      = &subpass,
        .dependencyCount = 1,
        .pDependencies   = &dep,
    };

    if (vkCreateRenderPass(ctx->device, &ci, nullptr,
                           &ctx->render_pass) != VK_SUCCESS) {
        fprintf(stderr, "[vulkan] failed to create render pass\n");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Graphics pipeline
// ---------------------------------------------------------------------------

bool vulkan_pipeline_create(VulkanContext *ctx,
                            const char *vert_path,
                            const char *frag_path) {
    if (!create_render_pass(ctx)) return false;

    // Load SPIR-V.
    size_t vert_size = 0, frag_size = 0;
    uint32_t *vert_code = read_spirv(vert_path, &vert_size);
    uint32_t *frag_code = read_spirv(frag_path, &frag_size);
    if (vert_code == nullptr || frag_code == nullptr) {
        free(vert_code); free(frag_code);
        return false;
    }

    VkShaderModule vert_mod = create_shader_module(ctx->device, vert_code, vert_size);
    VkShaderModule frag_mod = create_shader_module(ctx->device, frag_code, frag_size);
    free(vert_code);
    free(frag_code);

    if (vert_mod == VK_NULL_HANDLE || frag_mod == VK_NULL_HANDLE) {
        if (vert_mod != VK_NULL_HANDLE) vkDestroyShaderModule(ctx->device, vert_mod, nullptr);
        if (frag_mod != VK_NULL_HANDLE) vkDestroyShaderModule(ctx->device, frag_mod, nullptr);
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2] = {
        {
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vert_mod,
            .pName  = "main",
        },
        {
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = frag_mod,
            .pName  = "main",
        },
    };

    // No vertex input — vertices are hardcoded in the shader.
    VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };

    VkPipelineInputAssemblyStateCreateInfo input_assembly = {
        .sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE,
    };

    // Dynamic viewport and scissor.
    VkDynamicState dyn_states[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };
    VkPipelineDynamicStateCreateInfo dynamic_state = {
        .sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates    = dyn_states,
    };

    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount  = 1,
    };

    VkPipelineRasterizationStateCreateInfo rasterizer = {
        .sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .lineWidth   = 1.0f,
        .cullMode    = VK_CULL_MODE_BACK_BIT,
        .frontFace   = VK_FRONT_FACE_CLOCKWISE,
    };

    VkPipelineMultisampleStateCreateInfo multisample = {
        .sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };

    VkPipelineColorBlendAttachmentState blend_att = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
        .blendEnable    = VK_FALSE,
    };

    VkPipelineColorBlendStateCreateInfo blend = {
        .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments    = &blend_att,
    };

    VkPipelineLayoutCreateInfo layout_ci = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    };

    if (vkCreatePipelineLayout(ctx->device, &layout_ci, nullptr,
                               &ctx->pipeline_layout) != VK_SUCCESS) {
        fprintf(stderr, "[vulkan] failed to create pipeline layout\n");
        vkDestroyShaderModule(ctx->device, vert_mod, nullptr);
        vkDestroyShaderModule(ctx->device, frag_mod, nullptr);
        return false;
    }

    VkGraphicsPipelineCreateInfo pipeline_ci = {
        .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount          = 2,
        .pStages             = stages,
        .pVertexInputState   = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState      = &viewport_state,
        .pRasterizationState = &rasterizer,
        .pMultisampleState   = &multisample,
        .pColorBlendState    = &blend,
        .pDynamicState       = &dynamic_state,
        .layout              = ctx->pipeline_layout,
        .renderPass          = ctx->render_pass,
        .subpass             = 0,
    };

    VkResult res = vkCreateGraphicsPipelines(ctx->device, VK_NULL_HANDLE, 1,
                                             &pipeline_ci, nullptr,
                                             &ctx->graphics_pipeline);

    vkDestroyShaderModule(ctx->device, vert_mod, nullptr);
    vkDestroyShaderModule(ctx->device, frag_mod, nullptr);

    if (res != VK_SUCCESS) {
        fprintf(stderr, "[vulkan] failed to create graphics pipeline\n");
        return false;
    }

    printf("[vulkan] graphics pipeline created\n");
    return true;
}

void vulkan_pipeline_destroy(VulkanContext *ctx) {
    if (ctx->device == VK_NULL_HANDLE) return;

    if (ctx->graphics_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(ctx->device, ctx->graphics_pipeline, nullptr);
        ctx->graphics_pipeline = VK_NULL_HANDLE;
    }
    if (ctx->pipeline_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(ctx->device, ctx->pipeline_layout, nullptr);
        ctx->pipeline_layout = VK_NULL_HANDLE;
    }
    if (ctx->render_pass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(ctx->device, ctx->render_pass, nullptr);
        ctx->render_pass = VK_NULL_HANDLE;
    }
}

// ---------------------------------------------------------------------------
// Framebuffers
// ---------------------------------------------------------------------------

bool vulkan_framebuffers_create(VulkanContext *ctx) {
    ctx->swapchain.framebuffers =
        malloc(sizeof(VkFramebuffer) * ctx->swapchain.image_count);
    if (ctx->swapchain.framebuffers == nullptr) return false;

    for (uint32_t i = 0; i < ctx->swapchain.image_count; ++i) {
        VkFramebufferCreateInfo ci = {
            .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass      = ctx->render_pass,
            .attachmentCount = 1,
            .pAttachments    = &ctx->swapchain.image_views[i],
            .width           = ctx->swapchain.extent.width,
            .height          = ctx->swapchain.extent.height,
            .layers          = 1,
        };
        if (vkCreateFramebuffer(ctx->device, &ci, nullptr,
                                &ctx->swapchain.framebuffers[i]) != VK_SUCCESS) {
            fprintf(stderr, "[vulkan] failed to create framebuffer %u\n", i);
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Command pool, buffers & sync objects
// ---------------------------------------------------------------------------

bool vulkan_commands_create(VulkanContext *ctx) {
    VkCommandPoolCreateInfo pool_ci = {
        .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = ctx->graphics_family,
    };

    if (vkCreateCommandPool(ctx->device, &pool_ci, nullptr,
                            &ctx->command_pool) != VK_SUCCESS) {
        fprintf(stderr, "[vulkan] failed to create command pool\n");
        return false;
    }

    VkCommandBufferAllocateInfo alloc_info = {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = ctx->command_pool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = MAX_FRAMES_IN_FLIGHT,
    };

    if (vkAllocateCommandBuffers(ctx->device, &alloc_info,
                                 ctx->command_buffers) != VK_SUCCESS) {
        fprintf(stderr, "[vulkan] failed to allocate command buffers\n");
        return false;
    }

    VkSemaphoreCreateInfo sem_ci = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };
    VkFenceCreateInfo fence_ci = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (vkCreateSemaphore(ctx->device, &sem_ci, nullptr,
                              &ctx->image_available[i]) != VK_SUCCESS ||
            vkCreateSemaphore(ctx->device, &sem_ci, nullptr,
                              &ctx->render_finished[i]) != VK_SUCCESS ||
            vkCreateFence(ctx->device, &fence_ci, nullptr,
                          &ctx->in_flight[i]) != VK_SUCCESS) {
            fprintf(stderr, "[vulkan] failed to create sync objects\n");
            return false;
        }
    }

    printf("[vulkan] command pool + sync objects ready\n");
    return true;
}

void vulkan_commands_destroy(VulkanContext *ctx) {
    if (ctx->device == VK_NULL_HANDLE) return;

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (ctx->render_finished[i] != VK_NULL_HANDLE)
            vkDestroySemaphore(ctx->device, ctx->render_finished[i], nullptr);
        if (ctx->image_available[i] != VK_NULL_HANDLE)
            vkDestroySemaphore(ctx->device, ctx->image_available[i], nullptr);
        if (ctx->in_flight[i] != VK_NULL_HANDLE)
            vkDestroyFence(ctx->device, ctx->in_flight[i], nullptr);
    }

    if (ctx->command_pool != VK_NULL_HANDLE)
        vkDestroyCommandPool(ctx->device, ctx->command_pool, nullptr);
}
