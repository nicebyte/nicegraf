/**
 * Copyright (c) 2026 nicegraf contributors
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

// NOTE: this file is meant to be included at the bottom of the vulkan backend implementation in test mode!

#include "utest.h"

static ngfvk_sync_state empty_sync_state() {
  ngfvk_sync_state result;
  memset(&result, 0, sizeof(result));
  result.layout = VK_IMAGE_LAYOUT_UNDEFINED;
  return result;
}

#define test_stg_access_mask(expected_result, stages, accesses) { \
  const ngfvk_sync_barrier_masks m      = {accesses, stages}; \
  const uint32_t                 result = ngfvk_per_stage_access_mask(&m); \
  ASSERT_EQ(expected_result, result); \
}

#define test_barrier(sync_state, dsm, dam, expected_src_stage_mask, expected_src_access_mask, expected_src_layout, expected_dst_layout) { \
  ngfvk_barrier_data bar; \
  const ngfvk_sync_req sync_req = {{dam, dsm}, expected_dst_layout}; \
  const bool barrier_necessary = \
      ngfvk_sync_barrier(sync_state, &sync_req, &bar); \
  const bool barrier_expected = \
      expected_src_stage_mask != 0 || (expected_src_layout != expected_dst_layout); \
  if (!barrier_expected) { \
    ASSERT_FALSE(barrier_necessary); \
  } else { \
    ASSERT_TRUE(barrier_necessary); \
    ASSERT_EQ(expected_src_stage_mask, bar.src_stage_mask); \
    ASSERT_EQ(expected_src_access_mask, bar.src_access_mask); \
    ASSERT_EQ(dsm, bar.dst_stage_mask); \
    ASSERT_EQ(dam, bar.dst_access_mask); \
    ASSERT_EQ(expected_src_layout, bar.src_layout); \
    ASSERT_EQ(expected_dst_layout, bar.dst_layout); \
  } \
}

#define test_sync_req_merge(dst_req, src_req, success_expected, expected_stage_mask, expected_access_mask, expected_layout) { \
  const bool success = ngfvk_sync_req_merge(&dst_req, &src_req); \
  ASSERT_EQ(success_expected, success); \
  ASSERT_EQ(expected_stage_mask, dst_req.barrier_masks.stage_mask); \
  ASSERT_EQ(expected_access_mask, dst_req.barrier_masks.access_mask); \
  ASSERT_EQ(expected_layout, dst_req.layout); \
}

UTEST(vk_sync, barrier_attrib_TwGr) {
  ngfvk_sync_state sync_state = empty_sync_state();
  test_barrier(
      &sync_state,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_WRITE_BIT,
      0,
      0,
      VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_UNDEFINED);
  test_barrier(
      &sync_state,
      VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
      VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_WRITE_BIT,
      VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_UNDEFINED);
}

UTEST(vk_sync, barrier_index_TwGr) {
  ngfvk_sync_state sync_state = empty_sync_state();
  test_barrier(
      &sync_state,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_WRITE_BIT,
      0,
      0,
      VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_UNDEFINED);
  test_barrier(
      &sync_state,
      VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
      VK_ACCESS_INDEX_READ_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_WRITE_BIT,
      VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_UNDEFINED);
}

UTEST(vk_sync, barrier_texture_TwGr) {
  ngfvk_sync_state sync_state = empty_sync_state();
  test_barrier(
      &sync_state,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_WRITE_BIT,
      VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
      0,
      VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  test_barrier(
      &sync_state,
      (VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT),
      VK_ACCESS_SHADER_READ_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_WRITE_BIT,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

UTEST(vk_sync, barrier_index_TwGrGr) {
  ngfvk_sync_state sync_state = empty_sync_state();
  test_barrier(
      &sync_state,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_WRITE_BIT,
      VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
      0,
      VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  test_barrier(
      &sync_state,
      (VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT),
      VK_ACCESS_SHADER_READ_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_WRITE_BIT,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  test_barrier(
      &sync_state,
      (VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT),
      VK_ACCESS_SHADER_READ_BIT,
      0,
      0,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

UTEST(vk_sync, barrier_texture_TwGrGrCrCr) {
  ngfvk_sync_state sync_state = empty_sync_state();
  test_barrier(
      &sync_state,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_WRITE_BIT,
      VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
      0,
      VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  test_barrier(
      &sync_state,
      (VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT),
      VK_ACCESS_SHADER_READ_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_WRITE_BIT,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  test_barrier(
      &sync_state,
      (VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT),
      VK_ACCESS_SHADER_READ_BIT,
      0,
      0,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  test_barrier(
      &sync_state,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_ACCESS_SHADER_READ_BIT,
      (VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT),
      0,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  test_barrier(
      &sync_state,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_ACCESS_SHADER_READ_BIT,
      0,
      0,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

UTEST(vk_sync, barrier_texture_TwGrGwCr) {
  ngfvk_sync_state sync_state = empty_sync_state();
  test_barrier(
      &sync_state,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_WRITE_BIT,
      VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
      0,
      VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  test_barrier(
      &sync_state,
      (VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT),
      VK_ACCESS_SHADER_READ_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_WRITE_BIT,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  test_barrier(
      &sync_state,
      (VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
       VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT),
      (VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
       VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
       VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT),
      (VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT),
      VK_ACCESS_SHADER_READ_BIT,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
  test_barrier(
      &sync_state,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_ACCESS_SHADER_READ_BIT,
      (VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
       VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT),
      (VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
       VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
       VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT),
      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

UTEST(vk_sync, barrier_texture_GwTr) {
  ngfvk_sync_state sync_state = empty_sync_state();
  test_barrier(
      &sync_state,
      (VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
       VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT),
      (VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
       VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
       VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT),
      VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
      0,
      VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
  test_barrier(
      &sync_state,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_READ_BIT,
      (VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
       VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT),
      (VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
       VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
       VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT),
      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
}

UTEST(vk_sync, barrier_buffer_CwCw) {
  ngfvk_sync_state sync_state = empty_sync_state();
  test_barrier(
      &sync_state,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_ACCESS_SHADER_WRITE_BIT,
      0,
      0,
      VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_UNDEFINED);
  test_barrier(
      &sync_state,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_ACCESS_SHADER_WRITE_BIT,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_ACCESS_SHADER_WRITE_BIT,
      VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_UNDEFINED);
}

UTEST(vk_sync, barrier_buffer_GrCwCw) {
  ngfvk_sync_state sync_state = empty_sync_state();
  test_barrier(
      &sync_state,
      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
      VK_ACCESS_UNIFORM_READ_BIT,
      0,
      0,
      VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_UNDEFINED);
  test_barrier(
      &sync_state,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_ACCESS_SHADER_WRITE_BIT,
      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
      VK_ACCESS_UNIFORM_READ_BIT,
      VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_UNDEFINED);
  test_barrier(
      &sync_state,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_ACCESS_SHADER_WRITE_BIT,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_ACCESS_SHADER_WRITE_BIT,
      VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_UNDEFINED);
}

UTEST(vk_sync, barrier_buffer_GrCrCw) {
  ngfvk_sync_state sync_state = empty_sync_state();
  test_barrier(
      &sync_state,
      VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
      VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
      0,
      0,
      VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_UNDEFINED);
  test_barrier(
      &sync_state,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_ACCESS_SHADER_READ_BIT,
      0,
      0,
      VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_UNDEFINED);
  test_barrier(
      &sync_state,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_ACCESS_SHADER_WRITE_BIT,
      (VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT),
      (VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT),
      VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_UNDEFINED);
}

UTEST(vk_sync, barrier_buffer_CwGrGr) {
  ngfvk_sync_state sync_state = empty_sync_state();
  test_barrier(
      &sync_state,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_ACCESS_SHADER_WRITE_BIT,
      0, 0,
      VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_UNDEFINED);
  test_barrier(
      &sync_state,
      VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
      VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_ACCESS_SHADER_WRITE_BIT,
      VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_UNDEFINED);
  test_barrier(
      &sync_state,
      VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
      VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
      0,
      0,
      VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_UNDEFINED);
}

UTEST(vk_sync, barrier_buffer_CwGrCr) {
  ngfvk_sync_state sync_state = empty_sync_state();
  test_barrier(
      &sync_state,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_ACCESS_SHADER_WRITE_BIT,
      0,
      0,
      VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_UNDEFINED);
  test_barrier(
      &sync_state,
      VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
      VK_ACCESS_SHADER_READ_BIT,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_ACCESS_SHADER_WRITE_BIT,
      VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_UNDEFINED);
  test_barrier(
      &sync_state,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_ACCESS_SHADER_READ_BIT,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_ACCESS_SHADER_WRITE_BIT,
      VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_UNDEFINED);
}

UTEST(vk_sync, req_merge_concurrent_reads) {
  ngfvk_sync_req dst_req = {{0, 0}, VK_IMAGE_LAYOUT_UNDEFINED};
  static const ngfvk_sync_req src_reqs[] = {
      {.barrier_masks =
           {.access_mask = VK_ACCESS_SHADER_READ_BIT,
            .stage_mask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT},
       .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
      {.barrier_masks =
           {.access_mask = VK_ACCESS_SHADER_READ_BIT,
            .stage_mask  = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT},
       .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
  test_sync_req_merge(
      dst_req,
      src_reqs[0],
      true,
      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
      VK_ACCESS_SHADER_READ_BIT,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  test_sync_req_merge(
      dst_req,
      src_reqs[1],
      true,
      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_ACCESS_SHADER_READ_BIT,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

UTEST(vk_sync, req_merge_write) {
  ngfvk_sync_req dst_req = {{0, 0}, VK_IMAGE_LAYOUT_UNDEFINED};
  static const ngfvk_sync_req sync_reqs[] = {
      {.barrier_masks =
           {.access_mask = VK_ACCESS_SHADER_WRITE_BIT,
            .stage_mask  = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT},
       .layout = VK_IMAGE_LAYOUT_UNDEFINED},
  };
  test_sync_req_merge(
      dst_req,
      sync_reqs[0],
      true,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_ACCESS_SHADER_WRITE_BIT,
      VK_IMAGE_LAYOUT_UNDEFINED);
}

UTEST(vk_sync, req_merge_write_write) {
  ngfvk_sync_req dst_req = {{0, 0}, VK_IMAGE_LAYOUT_UNDEFINED};
  static const ngfvk_sync_req sync_reqs[] = {
      {.barrier_masks =
           {.access_mask = VK_ACCESS_SHADER_WRITE_BIT,
            .stage_mask  = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT},
       .layout = VK_IMAGE_LAYOUT_GENERAL},
     {.barrier_masks =
           {.access_mask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .stage_mask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT},
       .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}
  };
  test_sync_req_merge(
      dst_req,
      sync_reqs[0],
      true,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_ACCESS_SHADER_WRITE_BIT,
      VK_IMAGE_LAYOUT_GENERAL);
  test_sync_req_merge(
      dst_req,
      sync_reqs[1],
      false,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_ACCESS_SHADER_WRITE_BIT,
      VK_IMAGE_LAYOUT_GENERAL);
}

UTEST(vk_sync, req_merge_write_read) {
  ngfvk_sync_req dst_req = {{0, 0}, VK_IMAGE_LAYOUT_UNDEFINED};
  static const ngfvk_sync_req sync_reqs[] = {
      {.barrier_masks =
           {.access_mask = VK_ACCESS_SHADER_WRITE_BIT,
            .stage_mask  = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT},
       .layout = VK_IMAGE_LAYOUT_GENERAL},
     {.barrier_masks =
           {.access_mask = VK_ACCESS_SHADER_READ_BIT,
            .stage_mask  = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT},
       .layout = VK_IMAGE_LAYOUT_GENERAL}
  };
  test_sync_req_merge(
      dst_req,
      sync_reqs[0],
      true,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_ACCESS_SHADER_WRITE_BIT,
      VK_IMAGE_LAYOUT_GENERAL);
  test_sync_req_merge(
      dst_req,
      sync_reqs[1],
      true,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
      VK_IMAGE_LAYOUT_GENERAL);
}

UTEST(vk_sync, req_merge_read_write) {
  ngfvk_sync_req dst_req = {{0, 0}, VK_IMAGE_LAYOUT_UNDEFINED};
  static const ngfvk_sync_req sync_reqs[] = {
     {.barrier_masks =
           {.access_mask = VK_ACCESS_SHADER_READ_BIT,
            .stage_mask  = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT},
       .layout = VK_IMAGE_LAYOUT_GENERAL},
     {.barrier_masks =
           {.access_mask = VK_ACCESS_SHADER_WRITE_BIT,
            .stage_mask  = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT},
       .layout = VK_IMAGE_LAYOUT_GENERAL}
  };
  test_sync_req_merge(
      dst_req,
      sync_reqs[0],
      true,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_ACCESS_SHADER_READ_BIT,
      VK_IMAGE_LAYOUT_GENERAL);
  test_sync_req_merge(
      dst_req,
      sync_reqs[1],
      true,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
      VK_IMAGE_LAYOUT_GENERAL);
}

UTEST(vk_sync, req_merge_layout_change) {
  ngfvk_sync_req dst_req = {{0, 0}, VK_IMAGE_LAYOUT_UNDEFINED};
  static const ngfvk_sync_req sync_reqs[] = {
     {.barrier_masks =
           {.access_mask = VK_ACCESS_SHADER_READ_BIT,
            .stage_mask  = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT},
       .layout = VK_IMAGE_LAYOUT_GENERAL},
     {.barrier_masks =
           {.access_mask = VK_ACCESS_SHADER_READ_BIT,
            .stage_mask  = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT},
       .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}
  };
  test_sync_req_merge(
      dst_req,
      sync_reqs[0],
      true,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_ACCESS_SHADER_READ_BIT,
      VK_IMAGE_LAYOUT_GENERAL);
  test_sync_req_merge(
      dst_req,
      sync_reqs[1],
      true,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_ACCESS_SHADER_READ_BIT,
      VK_IMAGE_LAYOUT_GENERAL);
}

UTEST(vk_sync, stg_access_map) {
#define BITMASK3x8(b7, b6, b5, b4, b3, b2, b1, b0) (((b7) << 21) | ((b6) << 18) | ((b5) << 15) | ((b4) << 12) | ((b3) << 9) | ((b2) << 6) | ((b1) << 3) | (b0) )
  // clang-format: off
  test_stg_access_mask(
      BITMASK3x8(0b000, 0b000, 0b000, 0b000, 0b000, 0b000, 0b000, 0b001),
      VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
      VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT);
  test_stg_access_mask(
      BITMASK3x8(0b000, 0b000, 0b000, 0b000, 0b000, 0b000, 0b000, 0b010),
      VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
      VK_ACCESS_INDEX_READ_BIT);
  test_stg_access_mask(
      BITMASK3x8(0b000, 0b000, 0b000, 0b000, 0b000, 0b000, 0b001, 0b000),
      VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
      VK_ACCESS_SHADER_READ_BIT);
  test_stg_access_mask(
      BITMASK3x8(0b000, 0b000, 0b000, 0b000, 0b000, 0b000, 0b101, 0b000),
      VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
      VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT);
  test_stg_access_mask(
      BITMASK3x8(0b000, 0b000, 0b000, 0b000, 0b000, 0b101, 0b101, 0b000),
      VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
      VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT);
  test_stg_access_mask(
      BITMASK3x8(0b000, 0b000, 0b000, 0b000, 0b000, 0b101, 0b101, 0b000),
      VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
      VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
  test_stg_access_mask(
      BITMASK3x8(0b000, 0b000, 0b000, 0b000, 0b111, 0b101, 0b101, 0b000),
      VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
  test_stg_access_mask(
      BITMASK3x8(0b000, 0b000, 0b011, 0b011, 0b000, 0b000, 0b000, 0b000),
      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
  test_stg_access_mask(
      BITMASK3x8(0b000, 0b000, 0b000, 0b000, 0b000, 0b000, 0b000, 0b000),
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
  test_stg_access_mask(
      BITMASK3x8(0b000, 0b011, 0b000, 0b000, 0b000, 0b000, 0b000, 0b000),
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
  test_stg_access_mask(
      BITMASK3x8(0b001, 0b000, 0b000, 0b000, 0b000, 0b000, 0b000, 0b000),
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_READ_BIT);
  test_stg_access_mask(
      BITMASK3x8(0b010, 0b000, 0b000, 0b000, 0b000, 0b000, 0b000, 0b000),
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_WRITE_BIT);
  test_stg_access_mask(
      BITMASK3x8(0b011, 0b000, 0b000, 0b000, 0b000, 0b000, 0b000, 0b000),
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT);
  test_stg_access_mask(
      BITMASK3x8(0b000, 0b000, 0b000, 0b000, 0b000, 0b000, 0b000, 0b000),
      VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
      VK_ACCESS_SHADER_READ_BIT);
#undef BITMASK3x8
  // clang-format: on
}

UTEST_MAIN()
