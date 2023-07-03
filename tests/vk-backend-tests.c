/**
 * Copyright (c) 2023 nicegraf contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "../source/ngf-vk/vk_10.h"
#include "nicegraf.h"
#include "nicetest.h"
#include <string.h>

typedef struct vkCmdWaitEventsExpectedParams {
  uint32_t                     expectedEventCount;
  const VkEvent*               expectedEvents;
  VkPipelineStageFlags         expectedSrcStageMask;
  VkPipelineStageFlags         expectedDstStageMask;
  uint32_t                     expectedMemoryBarrierCount;
  const VkMemoryBarrier*       expectedMemoryBarriers;
  uint32_t                     expectedBufferMemoryBarrierCount;
  const VkBufferMemoryBarrier* expectedBufferMemoryBarriers;
  uint32_t                     expectedImageMemoryBarrierCount;
  const VkImageMemoryBarrier*  expectedImageMemoryBarriers;
} vkCmdWaitEventsExpectedParams;

uint32_t                             vkCmdWaitEventsExpectedNumberOfCalls = 0u;
const vkCmdWaitEventsExpectedParams* vkCmdWaitEventsExpectedParamsList    = NULL;

void VKAPI_CALL fake_wait_events(
    VkCommandBuffer              commandBuffer,
    uint32_t                     eventCount,
    const VkEvent*               pEvents,
    VkPipelineStageFlags         srcStageMask,
    VkPipelineStageFlags         dstStageMask,
    uint32_t                     memoryBarrierCount,
    const VkMemoryBarrier*       pMemoryBarriers,
    uint32_t                     bufferMemoryBarrierCount,
    const VkBufferMemoryBarrier* pBufferMemoryBarriers,
    uint32_t                     imageMemoryBarrierCount,
    const VkImageMemoryBarrier*  pImageMemoryBarriers) {
  (void)commandBuffer;

  NT_ASSERT(vkCmdWaitEventsExpectedNumberOfCalls > 0u);

  /* NOTE: first expected call should be the LAST in the expected params list. */

  const vkCmdWaitEventsExpectedParams* expectedParams =
      &vkCmdWaitEventsExpectedParamsList[--vkCmdWaitEventsExpectedNumberOfCalls];

  NT_ASSERT(eventCount == expectedParams->expectedEventCount);
  for (uint32_t e = 0u; e < eventCount; ++e) {
    NT_ASSERT(expectedParams->expectedEvents[e] == pEvents[e]);
  }

  NT_ASSERT(srcStageMask == expectedParams->expectedSrcStageMask);
  NT_ASSERT(dstStageMask == expectedParams->expectedDstStageMask);

  NT_ASSERT(memoryBarrierCount == expectedParams->expectedMemoryBarrierCount);
  for (uint32_t b = 0u; b < memoryBarrierCount; ++b) {
    const VkMemoryBarrier* b0         = &pMemoryBarriers[b];
    const VkMemoryBarrier* b1 = &expectedParams->expectedMemoryBarriers[b];
    NT_ASSERT(b0->sType == b1->sType);
    NT_ASSERT(b0->pNext == b1->pNext);
    NT_ASSERT(b0->srcAccessMask == b1->srcAccessMask);
    NT_ASSERT(b0->dstAccessMask == b1->dstAccessMask);
  }

  NT_ASSERT(bufferMemoryBarrierCount == expectedParams->expectedBufferMemoryBarrierCount);
  for (uint32_t b = 0u; b < bufferMemoryBarrierCount; ++b) {
    const VkBufferMemoryBarrier* b0         = &pBufferMemoryBarriers[b];
    const VkBufferMemoryBarrier* b1 = &expectedParams->expectedBufferMemoryBarriers[b];
    NT_ASSERT(b0->sType == b1->sType);
    NT_ASSERT(b0->pNext == b1->pNext);
    NT_ASSERT(b0->buffer == b1->buffer);
    NT_ASSERT(b0->offset == b1->offset);
    NT_ASSERT(b0->size == b1->size);
    NT_ASSERT(b0->srcAccessMask == b1->srcAccessMask);
    NT_ASSERT(b0->dstAccessMask == b1->dstAccessMask);
    NT_ASSERT(b0->srcQueueFamilyIndex == b1->srcQueueFamilyIndex);
    NT_ASSERT(b0->dstQueueFamilyIndex == b1->dstQueueFamilyIndex);
  }

  NT_ASSERT(imageMemoryBarrierCount == expectedParams->expectedImageMemoryBarrierCount);
  for (uint32_t b = 0u; b < bufferMemoryBarrierCount; ++b) {
    const VkImageMemoryBarrier* b0= &pImageMemoryBarriers[b];
    const VkImageMemoryBarrier* b1 = &expectedParams->expectedImageMemoryBarriers[b];
    NT_ASSERT(b0->sType == b1->sType);
    NT_ASSERT(b0->pNext == b1->pNext);
    NT_ASSERT(b0->srcAccessMask == b1->srcAccessMask);
    NT_ASSERT(b0->dstAccessMask == b1->dstAccessMask);
    NT_ASSERT(b0->srcQueueFamilyIndex == b1->srcQueueFamilyIndex);
    NT_ASSERT(b0->dstQueueFamilyIndex == b1->dstQueueFamilyIndex);
    NT_ASSERT(b0->subresourceRange.aspectMask == b1->subresourceRange.aspectMask);
    NT_ASSERT(b0->subresourceRange.baseArrayLayer == b1->subresourceRange.baseArrayLayer);
    NT_ASSERT(b0->subresourceRange.layerCount == b1->subresourceRange.layerCount);
    NT_ASSERT(b0->subresourceRange.levelCount == b1->subresourceRange.levelCount);
    NT_ASSERT(b0->subresourceRange.baseMipLevel == b1->subresourceRange.baseMipLevel);
    NT_ASSERT(b0->image == b1->image);
  }
}

NT_TESTSUITE {
  vkCmdWaitEvents = fake_wait_events;

  NT_TESTCASE(executeSyncOpNoResources) {
    vkCmdWaitEventsExpectedNumberOfCalls = 0u;
    ngf_cmd_buffer_t fake_cmd_buf;
    fake_cmd_buf.state = NGFI_CMD_BUFFER_READY;
    ngf_error error    = ngfvk_execute_sync_op(&fake_cmd_buf, 0u, NULL, 0u, NULL, 0u, NULL, 0u);
    NT_ASSERT(error == NGF_ERROR_OK);
  }

  NT_TESTCASE(executeSyncOpInvalidCmdBuffer) {
    vkCmdWaitEventsExpectedNumberOfCalls = 0u;
    ngf_cmd_buffer_t fake_cmd_buf;
    fake_cmd_buf.state = NGFI_CMD_BUFFER_RECORDING;
    ngf_error err      = ngfvk_execute_sync_op(&fake_cmd_buf, 0u, NULL, 0u, NULL, 0u, NULL, 0u);
    NT_ASSERT(err == NGF_ERROR_INVALID_OPERATION);
  }

  NT_TESTCASE(executeSyncOpSuperfluousSync) {
    vkCmdWaitEventsExpectedNumberOfCalls = 0u;

    ngf_cmd_buffer_t fake_cmd_buf;
    ngf_buffer_t     fake_buffer;
    memset(&fake_buffer, 0, sizeof(fake_buffer));
    fake_cmd_buf.state = NGFI_CMD_BUFFER_READY;

    /* gfx to gfx */
    ngf_sync_render_resource render_res = {
        .resource = {
            .sync_resource_type = NGF_SYNC_RESOURCE_BUFFER,
            .resource           = {.buffer_slice = {.buffer = &fake_buffer}}}};
    ngf_error err = ngfvk_execute_sync_op(
        &fake_cmd_buf,
        0u,
        NULL,
        1u,
        &render_res,
        0u,
        NULL,
        NGFVK_GFX_PIPELINE_STAGE_MASK);
    NT_ASSERT(err == NGF_ERROR_OK);

    /* gfx to xfer */
    ngf_sync_xfer_resource xfer_res = {
        .resource = {
            .sync_resource_type = NGF_SYNC_RESOURCE_BUFFER,
            .resource           = {.buffer_slice = {.buffer = &fake_buffer}}}};
    err = ngfvk_execute_sync_op(
        &fake_cmd_buf,
        0u,
        NULL,
        0u,
        NULL,
        1u,
        &xfer_res,
        NGFVK_GFX_PIPELINE_STAGE_MASK);
    NT_ASSERT(err == NGF_ERROR_OK);

    /* xfer to gfx */
    err = ngfvk_execute_sync_op(
        &fake_cmd_buf,
        0u,
        NULL,
        1u,
        &render_res,
        0u,
        NULL,
        VK_PIPELINE_STAGE_TRANSFER_BIT);
    NT_ASSERT(err == NGF_ERROR_OK);

    /* xfer to xfer */
    err = ngfvk_execute_sync_op(
        &fake_cmd_buf,
        0u,
        NULL,
        0u,
        NULL,
        1u,
        &xfer_res,
        VK_PIPELINE_STAGE_TRANSFER_BIT);
    NT_ASSERT(err == NGF_ERROR_OK);
  }

  /* gfx to compute */
  NT_TESTCASE(executeSyncOpGfxToCompute) {
    ngf_buffer_t fake_buffer;
    ngf_image_t  fake_image;
    ngf_cmd_buffer_t fake_cmd_buf;
    ngf_compute_encoder fake_compute_encoders[2] = {
        {.pvt_data_donotuse = {.d0 = 0xffffffff, .d1 = 0xdeadbeef}},
        {.pvt_data_donotuse = {.d0 = 0xffffffff, .d1 = 0xbaddf00d}}};

    fake_cmd_buf.state = NGFI_CMD_BUFFER_READY;
    memset(&fake_buffer, 0u, sizeof(fake_buffer));
    memset(&fake_image, 0u, sizeof(fake_image));
    fake_image.usage_flags = NGF_IMAGE_USAGE_STORAGE | NGF_IMAGE_USAGE_SAMPLE_FROM;
    fake_buffer.usage_flags = NGF_BUFFER_USAGE_STORAGE_BUFFER | NGF_BUFFER_USAGE_VERTEX_BUFFER;

    ngf_sync_compute_resource sync_compute_resources[] = {
        {.encoder = fake_compute_encoders[0],
         .resource =
             {.sync_resource_type = NGF_SYNC_RESOURCE_IMAGE,
              .resource = {.image_ref = {.image = &fake_image, .mip_level = 1u, .layer = 2u}}}},
        {.encoder = fake_compute_encoders[1],
         .resource =
             {.sync_resource_type = NGF_SYNC_RESOURCE_BUFFER,
              .resource =
                  {.buffer_slice = {.buffer = &fake_buffer, .offset = 256u, .range = 64u}

                  }}},
    };
    ngf_render_encoder       fake_render_encoder;
    memset(&fake_render_encoder, 0, sizeof(fake_render_encoder));
    ngf_sync_render_resource sync_render_resource = {
      .encoder  = fake_render_encoder,
       .resource = {
           .sync_resource_type = NGF_SYNC_RESOURCE_IMAGE,
           .resource = {.image_ref = {.image = &fake_image, .mip_level = 1u, .layer = 2u}}}};

    vkCmdWaitEventsExpectedNumberOfCalls = 1u;
    const VkEvent expectedEvents[]       = {
        (VkEvent)fake_compute_encoders[0].pvt_data_donotuse.d1,
        (VkEvent)fake_compute_encoders[1].pvt_data_donotuse.d1};
    const VkBufferMemoryBarrier expected_buffer_barrier = {
        .sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .pNext               = NULL,
        .buffer              = (VkBuffer)fake_buffer.alloc.obj_handle,
        .offset              = 256u,
        .size                = 64u,
        .dstAccessMask       = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_SHADER_READ_BIT,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .srcAccessMask =
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
    };
    const VkImageMemoryBarrier expected_image_barrier = {
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext               = NULL,
        .image               = (VkImage)fake_image.alloc.obj_handle,
        .dstAccessMask       = VK_ACCESS_SHADER_READ_BIT,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .srcAccessMask =
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .subresourceRange    = {
               .baseArrayLayer = 2u,
               .baseMipLevel   = 1u,
               .layerCount     = 1u,
               .levelCount     = 1u,
               .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT}};

    const vkCmdWaitEventsExpectedParams expected_params = {
        .expectedEventCount = 2u,
        .expectedEvents = expectedEvents,
        .expectedSrcStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        .expectedDstStageMask = NGFVK_GFX_PIPELINE_STAGE_MASK,
        .expectedMemoryBarrierCount = 0u,
        .expectedMemoryBarriers = NULL,
        .expectedBufferMemoryBarrierCount = 1u,
        .expectedBufferMemoryBarriers = &expected_buffer_barrier,
        .expectedImageMemoryBarrierCount = 1u,
        .expectedImageMemoryBarriers = &expected_image_barrier,
    };
    vkCmdWaitEventsExpectedParamsList = &expected_params;

    ngf_error err = ngfvk_execute_sync_op(
        &fake_cmd_buf,
        2u,
        sync_compute_resources,
        1u,
        &sync_render_resource, /* shouldnt result in any barriers.*/
        0u,
        NULL,
        NGFVK_GFX_PIPELINE_STAGE_MASK);
    NT_ASSERT(err == NGF_ERROR_OK);
    NT_ASSERT(vkCmdWaitEventsExpectedNumberOfCalls == 0u);
  }

  /* xfer to compute */
  NT_TESTCASE(executeSyncOpGfxToCompute) {
    ngf_buffer_t fake_buffer;
    ngf_image_t  fake_image;
    ngf_cmd_buffer_t fake_cmd_buf;
    ngf_compute_encoder fake_compute_encoders[2] = {
        {.pvt_data_donotuse = {.d0 = 0xffffffff, .d1 = 0xdeadbeef}},
        {.pvt_data_donotuse = {.d0 = 0xffffffff, .d1 = 0xbaddf00d}}};

    fake_cmd_buf.state = NGFI_CMD_BUFFER_READY;
    memset(&fake_buffer, 0u, sizeof(fake_buffer));
    memset(&fake_image, 0u, sizeof(fake_image));
    fake_image.usage_flags = NGF_IMAGE_USAGE_STORAGE | NGF_IMAGE_USAGE_SAMPLE_FROM | NGF_IMAGE_USAGE_XFER_DST;
    fake_buffer.usage_flags = NGF_BUFFER_USAGE_STORAGE_BUFFER | NGF_BUFFER_USAGE_XFER_SRC;

    ngf_sync_compute_resource sync_compute_resources[] = {
        {.encoder = fake_compute_encoders[0],
         .resource =
             {.sync_resource_type = NGF_SYNC_RESOURCE_IMAGE,
              .resource = {.image_ref = {.image = &fake_image, .mip_level = 1u, .layer = 2u}}}},
        {.encoder = fake_compute_encoders[1],
         .resource =
             {.sync_resource_type = NGF_SYNC_RESOURCE_BUFFER,
              .resource =
                  {.buffer_slice = {.buffer = &fake_buffer, .offset = 256u, .range = 64u}

                  }}},
    };
    ngf_render_encoder       fake_render_encoder;
    memset(&fake_render_encoder, 0, sizeof(fake_render_encoder));
    ngf_sync_render_resource sync_render_resource = {
      .encoder  = fake_render_encoder,
       .resource = {
           .sync_resource_type = NGF_SYNC_RESOURCE_IMAGE,
           .resource = {.image_ref = {.image = &fake_image, .mip_level = 1u, .layer = 2u}}}};

    vkCmdWaitEventsExpectedNumberOfCalls = 1u;
    const VkEvent expected_events[]       = {
        (VkEvent)fake_compute_encoders[0].pvt_data_donotuse.d1,
        (VkEvent)fake_compute_encoders[1].pvt_data_donotuse.d1};
    const VkBufferMemoryBarrier expected_buffer_barrier = {
        .sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .pNext               = NULL,
        .buffer              = (VkBuffer)fake_buffer.alloc.obj_handle,
        .offset              = 256u,
        .size                = 64u,
        .dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .srcAccessMask =
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
    };
    const VkImageMemoryBarrier expected_image_barrier = {
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext               = NULL,
        .image               = (VkImage)fake_image.alloc.obj_handle,
        .dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .srcAccessMask =
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .subresourceRange    = {
               .baseArrayLayer = 2u,
               .baseMipLevel   = 1u,
               .layerCount     = 1u,
               .levelCount     = 1u,
               .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT}};

    const vkCmdWaitEventsExpectedParams expected_params = {
        .expectedEventCount = 2u,
        .expectedEvents = expected_events,
        .expectedSrcStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        .expectedDstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT,
        .expectedMemoryBarrierCount = 0u,
        .expectedMemoryBarriers = NULL,
        .expectedBufferMemoryBarrierCount = 1u,
        .expectedBufferMemoryBarriers = &expected_buffer_barrier,
        .expectedImageMemoryBarrierCount = 1u,
        .expectedImageMemoryBarriers = &expected_image_barrier,
    };
    vkCmdWaitEventsExpectedParamsList = &expected_params;

    ngf_error err = ngfvk_execute_sync_op(
        &fake_cmd_buf,
        2u,
        sync_compute_resources,
        1u,
        &sync_render_resource, /* shouldnt result in any barriers.*/
        0u,
        NULL,
        VK_PIPELINE_STAGE_TRANSFER_BIT);
    NT_ASSERT(err == NGF_ERROR_OK);
    NT_ASSERT(vkCmdWaitEventsExpectedNumberOfCalls == 0u);
  }

  /* compute to gfx, compute and xfer*/
  NT_TESTCASE(executeSyncOpComputeToComputeGfxXfer) {
    ngf_buffer_t fake_buffers[3];
    ngf_image_t  fake_images[3];
    ngf_cmd_buffer_t fake_cmd_buf;
    ngf_compute_encoder fake_compute_encoder = {
        .pvt_data_donotuse = {.d0 = 0xffffffff, .d1 = 0xdeadbee0}};
    ngf_render_encoder fake_render_encoder = {
        .pvt_data_donotuse = {.d0 = 0xffffffff, .d1 = 0xdeadbee1}};
    ngf_xfer_encoder fake_xfer_encoder = {
        .pvt_data_donotuse = {.d0 = 0xffffffff, .d1 = 0xdeadbee2}};
    
    memset(fake_buffers, 0, sizeof(fake_buffers));
    memset(fake_images, 0, sizeof(fake_images));
    fake_buffers[0].usage_flags = NGF_BUFFER_USAGE_STORAGE_BUFFER | NGF_BUFFER_USAGE_UNIFORM_BUFFER;
    fake_buffers[1].usage_flags = NGF_BUFFER_USAGE_STORAGE_BUFFER | NGF_BUFFER_USAGE_INDEX_BUFFER;
    fake_buffers[2].usage_flags = NGF_BUFFER_USAGE_STORAGE_BUFFER | NGF_BUFFER_USAGE_XFER_SRC;
    fake_images[0].usage_flags  = NGF_IMAGE_USAGE_SAMPLE_FROM;
    fake_images[1].usage_flags  = NGF_IMAGE_USAGE_SAMPLE_FROM | NGF_IMAGE_USAGE_STORAGE;
    fake_images[2].usage_flags  = NGF_IMAGE_USAGE_SAMPLE_FROM | NGF_IMAGE_USAGE_XFER_DST;
    fake_images[0].vkformat     = VK_FORMAT_D24_UNORM_S8_UINT;
    fake_cmd_buf.state          = NGFI_CMD_BUFFER_READY;

    vkCmdWaitEventsExpectedNumberOfCalls = 1u;
    const VkEvent expected_events[]      = {
        (VkEvent)fake_compute_encoder.pvt_data_donotuse.d1,
        (VkEvent)fake_render_encoder.pvt_data_donotuse.d1,
        (VkEvent)fake_xfer_encoder.pvt_data_donotuse.d1,
    };
    const VkBufferMemoryBarrier expected_buffer_barriers[] = {
        {
            .sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .pNext               = NULL,
            .buffer              = (VkBuffer)fake_buffers[0].alloc.obj_handle,
            .offset              = 0u,
            .size                = 64u,
            .dstAccessMask       = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_UNIFORM_READ_BIT,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .srcAccessMask       = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_UNIFORM_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        },
        {
            .sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .pNext               = NULL,
            .buffer              = (VkBuffer)fake_buffers[1].alloc.obj_handle,
            .offset              = 0u,
            .size                = 64u,
            .dstAccessMask       = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .srcAccessMask       = VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_SHADER_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        },
        {
            .sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .pNext               = NULL,
            .buffer              = (VkBuffer)fake_buffers[2].alloc.obj_handle,
            .offset              = 0u,
            .size                = 64u,
            .dstAccessMask       = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .srcAccessMask       = VK_ACCESS_TRANSFER_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        }};
    const VkImageMemoryBarrier expected_image_barriers[] = {
        {.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
         .pNext               = NULL,
         .image               = (VkImage)fake_images[0].alloc.obj_handle,
         .dstAccessMask       = VK_ACCESS_SHADER_READ_BIT,
         .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .srcAccessMask       = VK_ACCESS_SHADER_READ_BIT,
         .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .subresourceRange =
             {.baseArrayLayer = 0u,
              .baseMipLevel   = 0u,
              .layerCount     = 1u,
              .levelCount     = 1u,
              .aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT }},
        {.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
         .pNext               = NULL,
         .image               = (VkImage)fake_images[1].alloc.obj_handle,
         .dstAccessMask       = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
         .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .srcAccessMask       = VK_ACCESS_SHADER_READ_BIT,
         .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .subresourceRange =
             {.baseArrayLayer = 0u,
              .baseMipLevel   = 0u,
              .layerCount     = 1u,
              .levelCount     = 1u,
              .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT}},
        {.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
         .pNext               = NULL,
         .image               = (VkImage)fake_images[2].alloc.obj_handle,
         .dstAccessMask       = VK_ACCESS_SHADER_READ_BIT,
         .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
         .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .subresourceRange    = {
                .baseArrayLayer = 0u,
                .baseMipLevel   = 0u,
                .layerCount     = 1u,
                .levelCount     = 1u,
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT}}};
    const vkCmdWaitEventsExpectedParams expected_params = {
        .expectedEventCount = 3u,
        .expectedEvents = expected_events,
        .expectedSrcStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | NGFVK_GFX_PIPELINE_STAGE_MASK | VK_PIPELINE_STAGE_TRANSFER_BIT,
        .expectedDstStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        .expectedMemoryBarrierCount = 0u,
        .expectedMemoryBarriers = NULL,
        .expectedBufferMemoryBarrierCount = 3u,
        .expectedBufferMemoryBarriers = expected_buffer_barriers,
        .expectedImageMemoryBarrierCount = 3u,
        .expectedImageMemoryBarriers = expected_image_barriers,
    };
    vkCmdWaitEventsExpectedParamsList = &expected_params;

    ngf_sync_compute_resource sync_compute_resources[] = {
        {.encoder = fake_compute_encoder,
         .resource =
             {.sync_resource_type = NGF_SYNC_RESOURCE_BUFFER,
              .resource =
                  {.buffer_slice = {.buffer = &fake_buffers[0], .offset = 0u, .range = 64u}}}},
        {.encoder  = fake_compute_encoder,
         .resource = {
             .sync_resource_type = NGF_SYNC_RESOURCE_IMAGE,
             .resource = {.image_ref = {.image = &fake_images[0], .layer = 0u, .mip_level = 0u}}}}};
    ngf_sync_render_resource sync_render_resources[] = {
        {.encoder = fake_render_encoder,
         .resource =
             {.sync_resource_type = NGF_SYNC_RESOURCE_BUFFER,
              .resource =
                  {.buffer_slice = {.buffer = &fake_buffers[1], .offset = 0u, .range = 64u}}}},
        {.encoder  = fake_render_encoder,
         .resource = {
             .sync_resource_type = NGF_SYNC_RESOURCE_IMAGE,
             .resource = {.image_ref = {.image = &fake_images[1], .layer = 0u, .mip_level = 0u}}}}};
    ngf_sync_xfer_resource sync_xfer_resources[] = {
        {.encoder = fake_xfer_encoder,
         .resource =
             {.sync_resource_type = NGF_SYNC_RESOURCE_BUFFER,
              .resource =
                  {.buffer_slice = {.buffer = &fake_buffers[2], .offset = 0u, .range = 64u}}}},
        {.encoder  = fake_xfer_encoder,
         .resource = {
             .sync_resource_type = NGF_SYNC_RESOURCE_IMAGE,
             .resource = {.image_ref = {.image = &fake_images[2], .layer = 0u, .mip_level = 0u}}}}};

    ngf_error err = ngfvk_execute_sync_op(
        &fake_cmd_buf,
        2u,
        sync_compute_resources,
        2u,
        sync_render_resources,
        2u,
        sync_xfer_resources,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    NT_ASSERT(err == NGF_ERROR_OK);
    NT_ASSERT(vkCmdWaitEventsExpectedNumberOfCalls == 0u);
  }
}