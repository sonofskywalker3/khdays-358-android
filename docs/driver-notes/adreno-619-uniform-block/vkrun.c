/* Diagnostic, never shipped: runs compose2d (one of its SPIR-V builds) outside the game on what the game gave
 * it (gfx_render.c KHDAYS_DUMP_INPUTS: engine<e>.bin), laid out as SDL3's GPU API lays a compute pipeline out,
 * and writes the picture it drew.
 *   vkrun <engine.bin> <compose.spv> <out.rgba> [key=value ...]
 *     rows=N      layers1x_rows given (0: the cached layers not read)
 *     src3d=N     source3d given (default 0: the 3D's targets are not in the dump)
 *     layers=FILE layers1x drawn here by this layers2d SPIR-V instead of the dump's copy; layersout=FILE writes it
 *     set1buf=N   read-write buffers after set 1's textures (the instrumented build: 1)
 *     word=I:V    parameter word I set to V (several) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

#define PARTS 10
#define CHECK(call)                                                                                                    \
    do {                                                                                                               \
        VkResult r_ = (call);                                                                                          \
        if (r_ != VK_SUCCESS) {                                                                                        \
            printf("%s: %d\n", #call, r_);                                                                             \
            exit(1);                                                                                                   \
        }                                                                                                              \
    } while (0)

static VkPhysicalDevice phys;
static VkDevice dev;
static VkQueue queue;
static VkCommandPool pool;
static VkPhysicalDeviceMemoryProperties memprops;
static VkCommandBuffer g_shared; /* seq=1: both passes recorded into this one, then submitted */
static uint32_t g_uoff; /* the parameters' place in their buffer (uoff=N), given as the dynamic offset */

static void *slurp(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    void *data;
    if (!f) {
        printf("cannot read %s\n", path);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    *size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    data = malloc(*size ? *size : 1);
    if (fread(data, 1, *size, f) != *size) {
        printf("short read of %s\n", path);
        exit(1);
    }
    fclose(f);
    return data;
}

static uint32_t memory_type(uint32_t bits, VkMemoryPropertyFlags want)
{
    for (uint32_t i = 0; i < memprops.memoryTypeCount; i++) {
        if ((bits & (1u << i)) && (memprops.memoryTypes[i].propertyFlags & want) == want) {
            return i;
        }
    }
    printf("no memory type\n");
    exit(1);
}

typedef struct Buffer {
    VkBuffer buffer;
    VkDeviceMemory memory;
    VkDeviceSize size;
    void *mapped;
} Buffer;

/* a buffer the processor can write and read, filled with `data` (zeros past it) */
static Buffer make_buffer(VkDeviceSize size, VkBufferUsageFlags usage, const void *data, size_t data_size)
{
    Buffer b;
    VkMemoryRequirements req;
    VkBufferCreateInfo info = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                               .size = size ? size : 16,
                               .usage = usage | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT};
    b.size = info.size;
    CHECK(vkCreateBuffer(dev, &info, NULL, &b.buffer));
    vkGetBufferMemoryRequirements(dev, b.buffer, &req);
    VkMemoryAllocateInfo alloc = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size,
        .memoryTypeIndex = memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
    CHECK(vkAllocateMemory(dev, &alloc, NULL, &b.memory));
    CHECK(vkBindBufferMemory(dev, b.buffer, b.memory, 0));
    CHECK(vkMapMemory(dev, b.memory, 0, VK_WHOLE_SIZE, 0, &b.mapped));
    memset(b.mapped, 0, (size_t)b.size);
    if (data != NULL && data_size != 0) {
        memcpy(b.mapped, data, data_size < b.size ? data_size : (size_t)b.size);
    }
    return b;
}

typedef struct Image {
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
    uint32_t w, h;
} Image;

static Image make_image(VkFormat format, uint32_t w, uint32_t h)
{
    Image im;
    VkMemoryRequirements req;
    VkImageCreateInfo info = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                              .imageType = VK_IMAGE_TYPE_2D,
                              .format = format,
                              .extent = {w, h, 1},
                              .mipLevels = 1,
                              .arrayLayers = 1,
                              .samples = VK_SAMPLE_COUNT_1_BIT,
                              .tiling = VK_IMAGE_TILING_OPTIMAL,
                              .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                       VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                              .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
    im.w = w;
    im.h = h;
    CHECK(vkCreateImage(dev, &info, NULL, &im.image));
    vkGetImageMemoryRequirements(dev, im.image, &req);
    VkMemoryAllocateInfo alloc = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                  .allocationSize = req.size,
                                  .memoryTypeIndex =
                                      memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};
    CHECK(vkAllocateMemory(dev, &alloc, NULL, &im.memory));
    CHECK(vkBindImageMemory(dev, im.image, im.memory, 0));
    VkImageViewCreateInfo view = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                  .image = im.image,
                                  .viewType = VK_IMAGE_VIEW_TYPE_2D,
                                  .format = format,
                                  .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    CHECK(vkCreateImageView(dev, &view, NULL, &im.view));
    return im;
}

static VkCommandBuffer begin(void)
{
    VkCommandBuffer cmd;
    VkCommandBufferAllocateInfo alloc = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                         .commandPool = pool,
                                         .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                         .commandBufferCount = 1};
    VkCommandBufferBeginInfo info = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    CHECK(vkAllocateCommandBuffers(dev, &alloc, &cmd));
    CHECK(vkBeginCommandBuffer(cmd, &info));
    return cmd;
}

static void finish(VkCommandBuffer cmd)
{
    VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd};
    CHECK(vkEndCommandBuffer(cmd));
    CHECK(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE));
    CHECK(vkQueueWaitIdle(queue));
    vkFreeCommandBuffers(dev, pool, 1, &cmd);
}

static void to_general(VkCommandBuffer cmd, Image *im, VkImageLayout from)
{
    VkImageMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                    .srcAccessMask = 0,
                                    .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT |
                                                     VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
                                    .oldLayout = from,
                                    .newLayout = VK_IMAGE_LAYOUT_GENERAL,
                                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                    .image = im->image,
                                    .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0,
                         NULL, 1, &barrier);
}

/* the image's texels from or to a buffer (4 bytes a texel) */
static void copy_image(Image *im, Buffer *b, int to_image)
{
    VkCommandBuffer cmd = begin();
    VkBufferImageCopy region = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                                .imageExtent = {im->w, im->h, 1}};
    if (to_image) {
        vkCmdCopyBufferToImage(cmd, b->buffer, im->image, VK_IMAGE_LAYOUT_GENERAL, 1, &region);
    } else {
        vkCmdCopyImageToBuffer(cmd, im->image, VK_IMAGE_LAYOUT_GENERAL, b->buffer, 1, &region);
    }
    finish(cmd);
}

typedef struct Pipeline {
    VkDescriptorSetLayout sets[3];
    VkPipelineLayout layout;
    VkPipeline pipeline;
} Pipeline;

static VkDescriptorSetLayout set_layout(uint32_t first_count, VkDescriptorType first_type, uint32_t second_count,
                                        VkDescriptorType second_type)
{
    VkDescriptorSetLayoutBinding b[16];
    uint32_t n = 0;
    VkDescriptorSetLayout layout;
    for (uint32_t i = 0; i < first_count + second_count; i++, n++) {
        b[n] = (VkDescriptorSetLayoutBinding){.binding = n,
                                              .descriptorType = i < first_count ? first_type : second_type,
                                              .descriptorCount = 1,
                                              .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT};
    }
    VkDescriptorSetLayoutCreateInfo info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                            .bindingCount = n,
                                            .pBindings = b};
    CHECK(vkCreateDescriptorSetLayout(dev, &info, NULL, &layout));
    return layout;
}

/* SDL3's compute layout: set 0 what is read (buffers), set 1 what is written (textures, then buffers), set 2
 * the uniform buffer (dynamic) */
static Pipeline make_pipeline(const char *spv_path, uint32_t buffers0, uint32_t images1, uint32_t buffers1)
{
    Pipeline p;
    size_t size;
    void *spv = slurp(spv_path, &size);
    VkShaderModule module;
    VkShaderModuleCreateInfo smi = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                    .codeSize = size,
                                    .pCode = (const uint32_t *)spv};
    CHECK(vkCreateShaderModule(dev, &smi, NULL, &module));
    p.sets[0] = set_layout(buffers0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    p.sets[1] = set_layout(images1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, buffers1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    p.sets[2] = set_layout(1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    VkPipelineLayoutCreateInfo pli = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                      .setLayoutCount = 3,
                                      .pSetLayouts = p.sets};
    CHECK(vkCreatePipelineLayout(dev, &pli, NULL, &p.layout));
    VkComputePipelineCreateInfo cpi = {.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
                                       .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                                 .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                                                 .module = module,
                                                 .pName = "cs_main"},
                                       .layout = p.layout};
    CHECK(vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cpi, NULL, &p.pipeline));
    free(spv);
    return p;
}

/* one dispatch of `p`: its buffers, its textures (then read-write buffers), its parameters */
static void run(Pipeline *p, Buffer *in, uint32_t nin, Image **images, uint32_t nimages, Buffer *rw, uint32_t nrw,
                Buffer *params, uint32_t gx, uint32_t gy)
{
    VkDescriptorPoolSize sizes[3] = {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 32},
                                     {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 32},
                                     {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 4}};
    VkDescriptorPoolCreateInfo dpi = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                                      .maxSets = 3,
                                      .poolSizeCount = 3,
                                      .pPoolSizes = sizes};
    VkDescriptorPool dpool;
    VkDescriptorSet sets[3];
    VkWriteDescriptorSet writes[40];
    VkDescriptorBufferInfo binfo[40];
    VkDescriptorImageInfo iinfo[40];
    uint32_t n = 0;
    const uint32_t offset = params->size > 4096 ? g_uoff : 0;
    VkCommandBuffer cmd;
    CHECK(vkCreateDescriptorPool(dev, &dpi, NULL, &dpool));
    VkDescriptorSetAllocateInfo dai = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                                       .descriptorPool = dpool,
                                       .descriptorSetCount = 3,
                                       .pSetLayouts = p->sets};
    CHECK(vkAllocateDescriptorSets(dev, &dai, sets));
    for (uint32_t i = 0; i < nin; i++, n++) {
        binfo[n] = (VkDescriptorBufferInfo){in[i].buffer, 0, VK_WHOLE_SIZE};
        writes[n] = (VkWriteDescriptorSet){.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                           .dstSet = sets[0],
                                           .dstBinding = i,
                                           .descriptorCount = 1,
                                           .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                           .pBufferInfo = &binfo[n]};
    }
    for (uint32_t i = 0; i < nimages; i++, n++) {
        iinfo[n] = (VkDescriptorImageInfo){VK_NULL_HANDLE, images[i]->view, VK_IMAGE_LAYOUT_GENERAL};
        writes[n] = (VkWriteDescriptorSet){.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                           .dstSet = sets[1],
                                           .dstBinding = i,
                                           .descriptorCount = 1,
                                           .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                           .pImageInfo = &iinfo[n]};
    }
    for (uint32_t i = 0; i < nrw; i++, n++) {
        binfo[n] = (VkDescriptorBufferInfo){rw[i].buffer, 0, VK_WHOLE_SIZE};
        writes[n] = (VkWriteDescriptorSet){.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                           .dstSet = sets[1],
                                           .dstBinding = nimages + i,
                                           .descriptorCount = 1,
                                           .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                           .pBufferInfo = &binfo[n]};
    }
    binfo[n] = (VkDescriptorBufferInfo){params->buffer, 0, params->size > 4096 ? 4096 : 256};
    writes[n] = (VkWriteDescriptorSet){.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                       .dstSet = sets[2],
                                       .dstBinding = 0,
                                       .descriptorCount = 1,
                                       .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                                       .pBufferInfo = &binfo[n]};
    n++;
    vkUpdateDescriptorSets(dev, n, writes, 0, NULL);
    cmd = g_shared != VK_NULL_HANDLE ? g_shared : begin();
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->layout, 0, 2, sets, 0, NULL);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->layout, 2, 1, &sets[2], 1, &offset);
    vkCmdDispatch(cmd, gx, gy, 1);
    if (g_shared == VK_NULL_HANDLE) {
        finish(cmd);
        vkDestroyDescriptorPool(dev, dpool, NULL);
    }
}

static void write_file(const char *path, const void *header, size_t header_size, const void *data, size_t size)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        printf("cannot write %s\n", path);
        exit(1);
    }
    if (header_size) {
        fwrite(header, 1, header_size, f);
    }
    fwrite(data, 1, size, f);
    fclose(f);
}

int main(int argc, char **argv)
{
    size_t file_size;
    const uint32_t *file;
    const uint32_t *sizes;
    const uint8_t *part[PARTS];
    uint32_t params[64];
    const char *layers_spv = NULL, *layers_out = NULL;
    uint32_t set1buf = 0;
    int seq = 0, frames = 1, barrier = 0;
    uint32_t lgx = 256 / 8, lgy = 192 / 8, lbuf = 8, limg = 1;
    int twice = 0, after = 0, mid = 0, corder = 0, lcopy = 0;
    uint32_t lwords[64] = {192, 0, 0, 0}; /* layers2d's parameters (lword=I:V), 256 bytes of them */
    uint32_t fill = 0; /* fill=V: the uniform buffer's other bytes */
    const char *mid_spv = NULL;
    if (argc < 4) {
        printf("usage: vkrun engine.bin compose.spv out.rgba [key=value ...]\n");
        return 2;
    }
    file = (const uint32_t *)slurp(argv[1], &file_size);
    if (file[0] != 0x4e49444bu || file[1] != PARTS || file[2] > sizeof(params)) {
        printf("not a dump of the inputs\n");
        return 1;
    }
    sizes = file + 4;
    memset(params, 0, sizeof(params));
    memcpy(params, sizes + PARTS, file[2]);
    {
        const uint8_t *at = (const uint8_t *)(sizes + PARTS) + file[2];
        for (int k = 0; k < PARTS; k++) {
            part[k] = at;
            at += sizes[k];
        }
    }
    printf("dump: part %u, scale %u, %ux%u, source3d %u, row0 %u, layers_off 0x%x, layers1x_rows %u, pre_mode %u\n",
           file[3], params[0], params[1], params[2], params[3], params[32], params[33], params[34], params[35]);
    params[3] = 0;
    for (int i = 4; i < argc; i++) {
        if (strncmp(argv[i], "rows=", 5) == 0) {
            params[34] = (uint32_t)atoi(argv[i] + 5);
        } else if (strncmp(argv[i], "src3d=", 6) == 0) {
            params[3] = (uint32_t)atoi(argv[i] + 6);
        } else if (strncmp(argv[i], "layers=", 7) == 0) {
            layers_spv = argv[i] + 7;
        } else if (strncmp(argv[i], "layersout=", 10) == 0) {
            layers_out = argv[i] + 10;
        } else if (strncmp(argv[i], "seq=", 4) == 0) {
            seq = atoi(argv[i] + 4);
        } else if (strncmp(argv[i], "barrier=", 8) == 0) {
            barrier = atoi(argv[i] + 8);
        } else if (strncmp(argv[i], "lbuf=", 5) == 0) {
            lbuf = (uint32_t)atoi(argv[i] + 5);
        } else if (strncmp(argv[i], "limg=", 5) == 0) {
            limg = (uint32_t)atoi(argv[i] + 5);
        } else if (strncmp(argv[i], "lword=", 6) == 0) {
            const char *colon = strchr(argv[i], ':');
            if (colon != NULL) {
                lwords[atoi(argv[i] + 6) & 63] = (uint32_t)strtoul(colon + 1, NULL, 0);
            }
        } else if (strncmp(argv[i], "fill=", 5) == 0) {
            fill = (uint32_t)strtoul(argv[i] + 5, NULL, 0);
        } else if (strncmp(argv[i], "lcopy=", 6) == 0) {
            lcopy = atoi(argv[i] + 6);
        } else if (strncmp(argv[i], "mid=", 4) == 0) {
            mid = atoi(argv[i] + 4);
        } else if (strncmp(argv[i], "midspv=", 7) == 0) {
            mid_spv = argv[i] + 7;
        } else if (strncmp(argv[i], "corder=", 7) == 0) {
            corder = atoi(argv[i] + 7);
        } else if (strncmp(argv[i], "twice=", 6) == 0) {
            twice = atoi(argv[i] + 6);
        } else if (strncmp(argv[i], "after=", 6) == 0) {
            after = atoi(argv[i] + 6);
        } else if (strncmp(argv[i], "lgx=", 4) == 0) {
            lgx = (uint32_t)atoi(argv[i] + 4);
        } else if (strncmp(argv[i], "lgy=", 4) == 0) {
            lgy = (uint32_t)atoi(argv[i] + 4);
        } else if (strncmp(argv[i], "frames=", 7) == 0) {
            frames = atoi(argv[i] + 7);
        } else if (strncmp(argv[i], "uoff=", 5) == 0) {
            g_uoff = (uint32_t)atoi(argv[i] + 5);
        } else if (strncmp(argv[i], "set1buf=", 8) == 0) {
            set1buf = (uint32_t)atoi(argv[i] + 8);
        } else if (strncmp(argv[i], "word=", 5) == 0) {
            const char *colon = strchr(argv[i], ':');
            if (colon != NULL) {
                params[atoi(argv[i] + 5) & 63] = (uint32_t)strtoul(colon + 1, NULL, 0);
            }
        }
    }

    /* REMAP in the environment: the parameters as the experimental build's block has them, the last four
     * (row0, layers_off, layers1x_rows, pre_mode) first */
    uint32_t uparams[64];
    memcpy(uparams, params, sizeof(uparams));
    if (getenv("REMAP") != NULL && atoi(getenv("REMAP")) == 3) {
        /* the block of 32 words as committed after 2026-11-10: `modes` in source3d's place */
        memset(uparams, 0, sizeof(uparams));
        memcpy(uparams, params, 27 * 4); /* scale .. height3d */
        uparams[3] = (params[3] & 3u) | (params[27] ? 4u : 0u) | (params[28] ? 8u : 0u) | (params[35] & 3u) << 4;
        uparams[27] = params[29];
        uparams[28] = params[30];
        uparams[29] = params[32];
        uparams[30] = params[33];
        uparams[31] = params[34];
    } else if (getenv("REMAP") != NULL && atoi(getenv("REMAP")) == 2) {
        memcpy(uparams + 28, params + 32, 16); /* the block of 32 words: the HUD's four out */
        memset(uparams + 32, 0, 16);
    } else if (getenv("REMAP") != NULL) {
        memcpy(uparams, params + 32, 16);
        memcpy(uparams + 4, params, 128);
    }
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_1};
    VkInstanceCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app};
    VkInstance inst;
    uint32_t count = 1, qcount = 16, family = 0;
    VkQueueFamilyProperties q[16];
    const float priority = 1.0f;
    CHECK(vkCreateInstance(&ici, NULL, &inst));
    vkEnumeratePhysicalDevices(inst, &count, &phys);
    vkGetPhysicalDeviceMemoryProperties(phys, &memprops);
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &qcount, q);
    for (uint32_t i = 0; i < qcount; i++) {
        if (q[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            family = i;
            break;
        }
    }
    VkDeviceQueueCreateInfo qci = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                   .queueFamilyIndex = family,
                                   .queueCount = 1,
                                   .pQueuePriorities = &priority};
    VkDeviceCreateInfo dci = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                              .queueCreateInfoCount = 1,
                              .pQueueCreateInfos = &qci};
    CHECK(vkCreateDevice(phys, &dci, NULL, &dev));
    vkGetDeviceQueue(dev, family, 0, &queue);
    VkCommandPoolCreateInfo cpi = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = family};
    CHECK(vkCreateCommandPool(dev, &cpi, NULL, &pool));

    Buffer in[8];
    for (int k = 0; k < 8; k++) {
        /* ZERO=mask in the environment: those buffers all zeros (none of the game's data); KEEP0=bytes: only
         * that much of buffer 0 kept */
        const int zero = getenv("ZERO") != NULL && ((atoi(getenv("ZERO")) >> k) & 1);
        size_t keep = sizes[k];
        if (k == 0 && getenv("KEEP0") != NULL && (size_t)atoi(getenv("KEEP0")) < keep) {
            keep = (size_t)atoi(getenv("KEEP0"));
        }
        in[k] = make_buffer(sizes[k], VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, zero ? NULL : part[k], zero ? 0 : keep);
    }
    /* lcopy=1: layers2d's pass given its own copies of the buffers; lcopy=2: all zeros in them */
    Buffer lin[8];
    for (int k = 0; k < 8; k++) {
        lin[k] = lcopy ? make_buffer(sizes[k], VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, lcopy == 1 ? part[k] : NULL,
                                     lcopy == 1 ? sizes[k] : 0)
                       : in[k];
    }
    const uint32_t w = params[1], h = params[2];
    Image dummy3d[2] = {make_image(VK_FORMAT_R32_UINT, 1, 1), make_image(VK_FORMAT_R32_UINT, 1, 1)};
    Image dummy_hud[2] = {make_image(VK_FORMAT_R32_UINT, 1, 1), make_image(VK_FORMAT_R32_UINT, 1, 1)};
    Image screen = make_image(VK_FORMAT_R8G8B8A8_UNORM, w, h);
    Image layers1x = make_image(VK_FORMAT_R32_UINT, 256, 768);
    Image pre = make_image(VK_FORMAT_R32_UINT, 2048, 768);
    Image *all[7] = {&dummy3d[0], &dummy3d[1], &screen, &dummy_hud[0], &dummy_hud[1], &layers1x, &pre};
    {
        VkCommandBuffer cmd = begin();
        for (int i = 0; i < 7; i++) {
            to_general(cmd, all[i], VK_IMAGE_LAYOUT_UNDEFINED);
        }
        finish(cmd);
    }
    Buffer layers_stage = make_buffer(256u * 768u * 4u, 0, part[8], sizes[8]);
    if (seq && layers_spv != NULL) {
        /* as the game: layers2d's pass then compose2d's in one command buffer, frame after frame; the screen
         * cleared to blue before each, and each frame's picture set beside the first's */
        Pipeline cp_first, lp, cp, mp;
        if (corder) {
            cp_first = make_pipeline(argv[2], 8, 7, set1buf); /* compose2d's made before layers2d's */
        }
        lp = make_pipeline(layers_spv, lbuf, limg, 0);
        cp = corder ? cp_first : make_pipeline(argv[2], 8, 7, set1buf);
        if (mid_spv != NULL) {
            mp = make_pipeline(mid_spv, 0, 2, 0); /* shrink.hlsl: two textures */
        }
        Image mid_a = make_image(VK_FORMAT_R8G8B8A8_UNORM, 8, 8), mid_b = make_image(VK_FORMAT_R8G8B8A8_UNORM, 8, 8);
        Image *mimages[2] = {&mid_a, &mid_b};
        Buffer mid_out = make_buffer(256u * 768u * 4u, 0, NULL, 0);
        {
            VkCommandBuffer c = begin();
            to_general(c, &mid_a, VK_IMAGE_LAYOUT_UNDEFINED);
            to_general(c, &mid_b, VK_IMAGE_LAYOUT_UNDEFINED);
            finish(c);
        }
        Buffer ubo = make_buffer(1u << 20, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, NULL, 0);
        Buffer lubo = make_buffer(256, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, lwords, sizeof(lwords));
        Buffer rw = make_buffer(1u << 20, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, NULL, 0);
        if (fill != 0) {
            for (uint32_t i = 0; i < (1u << 20) / 4u; i++) {
                ((uint32_t *)ubo.mapped)[i] = fill;
            }
        }
        Buffer out = make_buffer((VkDeviceSize)w * h * 4u, 0, NULL, 0);
        Image *limages[2] = {&layers1x, &pre}; /* (another shader in layers2d's place: its one or two textures) */
        uint32_t *first = malloc((size_t)w * h * 4u);
        int unlike = 0;
        memcpy((uint8_t *)ubo.mapped + g_uoff, uparams, 256);
        for (int f = 0; f < frames; f++) {
            const VkClearColorValue blue = {.float32 = {1.0f, 0.0f, 1.0f, 1.0f}}; /* magenta: no colour of the game's */
            const VkImageSubresourceRange range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            VkCommandBuffer cmd = begin();
            vkCmdClearColorImage(cmd, screen.image, VK_IMAGE_LAYOUT_GENERAL, &blue, 1, &range);
            g_shared = cmd;
            if (lgx != 0 && lgy != 0 && !after) {
                run(&lp, lin, lbuf, limages, limg, NULL, 0, &lubo, lgx, lgy);
            }
            if (mid == 1 && mid_spv != NULL) {
                /* a small pass of another shader between the two */
                run(&mp, in, 0, mimages, 2, NULL, 0, &lubo, 1, 1);
            } else if (mid == 2) {
                /* a copy between the two */
                VkBufferImageCopy region = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                                            .imageExtent = {256, 768, 1}};
                vkCmdCopyImageToBuffer(cmd, layers1x.image, VK_IMAGE_LAYOUT_GENERAL, mid_out.buffer, 1, &region);
            }
            if (seq == 3) {
                /* the first pass submitted and waited for by itself */
                g_shared = VK_NULL_HANDLE;
                finish(cmd);
                cmd = begin();
                g_shared = cmd;
            }
            if (twice) {
                run(&cp, in, 8, all, 7, &rw, set1buf, &ubo, (w + 7) / 8, (h + 7) / 8);
            }
            if (barrier) {
                /* what layers2d wrote, waited for before compose2d reads it */
                VkMemoryBarrier mb = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                      .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                                      .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                     0, 1, &mb, 0, NULL, 0, NULL);
            }
            run(&cp, in, 8, all, 7, &rw, set1buf, &ubo, (w + 7) / 8, (h + 7) / 8);
            if (after) {
                run(&lp, in, lbuf, limages, limg, NULL, 0, &lubo, lgx, lgy);
            }
            g_shared = VK_NULL_HANDLE;
            if (seq == 2) {
                /* the picture copied out in the same command buffer, as the game's dump did */
                VkBufferImageCopy region = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                                            .imageExtent = {w, h, 1}};
                vkCmdCopyImageToBuffer(cmd, screen.image, VK_IMAGE_LAYOUT_GENERAL, out.buffer, 1, &region);
                finish(cmd);
            } else {
                finish(cmd);
                copy_image(&screen, &out, 0);
            }
            if (f == 0) {
                memcpy(first, out.mapped, (size_t)w * h * 4u);
            } else if (memcmp(first, out.mapped, (size_t)w * h * 4u) != 0) {
                unlike++;
            }
        }
        printf("%d frames, %d unlike the first\n", frames, unlike);
        {
            /* the texels the pass never wrote: still the blue they were cleared to */
            const uint32_t *px = (const uint32_t *)out.mapped;
            uint32_t unwritten = 0, first_row = 0xffffffffu, last_row = 0;
            for (uint32_t i = 0; i < w * h; i++) {
                if (px[i] == 0xffff00ffu) {
                    unwritten++;
                    if (first_row == 0xffffffffu) {
                        first_row = i / w;
                    }
                    last_row = i / w;
                }
            }
            printf("unwritten texels: %u of %u (rows %d..%d)\n", unwritten, w * h, (int)first_row, (int)last_row);
        }
        {
            const uint32_t header[2] = {w, h};
            write_file(argv[3], header, sizeof(header), out.mapped, (size_t)w * h * 4u);
        }
        return 0;
    }
    if (layers_spv != NULL) {
        /* layers2d.hlsl's pass, as gfx_render.c dispatches it */
        Pipeline lp = make_pipeline(layers_spv, 8, 1, 0);
        const uint32_t lparams[4] = {192, 0, 0, 0};
        Buffer lubo = make_buffer(256, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, lparams, sizeof(lparams));
        Image *limages[1] = {&layers1x};
        run(&lp, in, 8, limages, 1, NULL, 0, &lubo, 256 / 8, 192 / 8);
        Buffer back = make_buffer(256u * 768u * 4u, 0, NULL, 0);
        copy_image(&layers1x, &back, 0);
        {
            const uint32_t *mine = (const uint32_t *)back.mapped, *theirs = (const uint32_t *)part[8];
            uint32_t differing = 0;
            for (uint32_t i = 0; i < 256u * 768u; i++) {
                differing += mine[i] != theirs[i];
            }
            printf("layers1x drawn here: %u texels differ from the game's\n", differing);
        }
        if (layers_out != NULL) {
            write_file(layers_out, NULL, 0, back.mapped, 256u * 768u * 4u);
        }
    } else {
        copy_image(&layers1x, &layers_stage, 1);
    }

    Pipeline cp = make_pipeline(argv[2], 8, 7, set1buf);
    Buffer ubo = make_buffer(1u << 20, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, NULL, 0);
    if (g_uoff >= 256) {
        /* as SDL leaves it: layers2d's parameters pushed before these */
        const uint32_t before[4] = {192, 0, 0, 0};
        memcpy((uint8_t *)ubo.mapped + g_uoff - 256, before, sizeof(before));
    }
    memcpy((uint8_t *)ubo.mapped + g_uoff, uparams, 256);
    Buffer rw = make_buffer(1u << 20, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, NULL, 0);
    run(&cp, in, 8, all, 7, &rw, set1buf, &ubo, (w + 7) / 8, (h + 7) / 8);

    Buffer out = make_buffer((VkDeviceSize)w * h * 4u, 0, NULL, 0);
    copy_image(&screen, &out, 0);
    {
        const uint32_t header[2] = {w, h};
        const uint32_t *mine = (const uint32_t *)out.mapped, *theirs = (const uint32_t *)part[9];
        uint32_t differing = 0;
        for (uint32_t i = 0; i < w * h; i++) {
            differing += mine[i] != theirs[i];
        }
        printf("screen: %ux%u, %u of %u pixels differ from the game's\n", w, h, differing, w * h);
        write_file(argv[3], header, sizeof(header), out.mapped, (size_t)w * h * 4u);
    }
    return 0;
}
