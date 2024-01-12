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

static ngfvk_sync_state empty_sync_state() {
  ngfvk_sync_state result;
  memset(&result, 0, sizeof(result));
  result.layout = VK_IMAGE_LAYOUT_UNDEFINED;
  // result.last_writer_stage = NGFVK_SYNC_STAGE_COUNT;
  return result;
}

static void test_barrier(
    ngfvk_sync_state*    sync_state,
    VkPipelineStageFlags dst_stage_mask,
    VkAccessFlags        dst_access_mask,
    VkPipelineStageFlags expected_src_stage_mask,
    VkAccessFlags        expected_src_access_mask,
    VkImageLayout        expected_src_layout,
    VkImageLayout        expected_dst_layout) {
  ngfvk_barrier_data bar;

  const ngfvk_sync_req sync_req = {{dst_access_mask, dst_stage_mask}, expected_dst_layout};
  const bool barrier_necessary =
      ngfvk_sync_barrier(sync_state, &sync_req, &bar);
  const bool barrier_expected =
      expected_src_stage_mask != 0 || (expected_src_layout != expected_dst_layout);
  if (!barrier_expected) {
    NT_ASSERT(!barrier_necessary);
  } else {
    NT_ASSERT(barrier_necessary);
    NT_ASSERT(bar.src_stage_mask == expected_src_stage_mask);
    NT_ASSERT(bar.src_access_mask == expected_src_access_mask);
    NT_ASSERT(bar.dst_stage_mask == dst_stage_mask);
    NT_ASSERT(bar.dst_access_mask == dst_access_mask);
    NT_ASSERT(bar.src_layout == expected_src_layout);
    NT_ASSERT(bar.dst_layout == expected_dst_layout);
  }
}

static void test_sync_req_merge(ngfvk_sync_req* dst_req, const ngfvk_sync_req* src_req,
    bool success_expected,
    VkPipelineStageFlags  expected_stage_mask,
    VkAccessFlags         expected_access_mask,
    VkImageLayout         expected_layout) {
  const bool success = ngfvk_sync_req_merge(dst_req, src_req);
  NT_ASSERT(success_expected == success);
  NT_ASSERT(expected_stage_mask == dst_req->barrier_masks.stage_mask);
  NT_ASSERT(expected_access_mask == dst_req->barrier_masks.access_mask);
  NT_ASSERT(expected_layout == dst_req->layout);
}

NT_TESTSUITE {
  NT_TESTCASE("sync_barrier - attrib TwGr") {
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

  NT_TESTCASE("sync_barrier - index TwGr") {
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

  NT_TESTCASE("sync_barrier - texture TwGr") {
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

  NT_TESTCASE("sync_barrier - index TwGrGr") {
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

  NT_TESTCASE("sync_barrier - texture TwGrGrCrCr") {
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

  NT_TESTCASE("sync_barrier - texture TwGrGwCr") {
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

  NT_TESTCASE("sync_barrier - texture GwTr") {
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

  NT_TESTCASE("sync_barrier - buffer CwCw") {
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

  NT_TESTCASE("sync_barrier - buffer GrCwCw") {
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

  NT_TESTCASE("sync_barrier - buffer GrCrCw") {
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

  NT_TESTCASE("sync barrier - buffer CwGrGr") {
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

  NT_TESTCASE("sync barrier - buffer CwGrCr") {
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

  NT_TESTCASE("sync req merge - concurrent reads") {
    ngfvk_sync_req dst_req = {{0, 0}, VK_IMAGE_LAYOUT_UNDEFINED};
    static const ngfvk_sync_req src_reqs[] = {
        {.barrier_masks =
             {.stage_mask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
              .access_mask = VK_ACCESS_SHADER_READ_BIT},
         .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {.barrier_masks =
             {.stage_mask  = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
              .access_mask = VK_ACCESS_SHADER_READ_BIT},
         .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
    test_sync_req_merge(
        &dst_req,
        &src_reqs[0],
        true,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_ACCESS_SHADER_READ_BIT,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    test_sync_req_merge(
        &dst_req,
        &src_reqs[1],
        true,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_SHADER_READ_BIT,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  }


  NT_TESTCASE("sync req merge - write") {
    ngfvk_sync_req dst_req = {{0, 0}, VK_IMAGE_LAYOUT_UNDEFINED};
    static const ngfvk_sync_req sync_reqs[] = {
        {.barrier_masks =
             {.stage_mask  = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
              .access_mask = VK_ACCESS_SHADER_WRITE_BIT},
         .layout = VK_IMAGE_LAYOUT_UNDEFINED},
    };
    test_sync_req_merge(
        &dst_req,
        &sync_reqs[0],
        true,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_SHADER_WRITE_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED);
  }


  NT_TESTCASE("sync req merge - write/write") {
    ngfvk_sync_req dst_req = {{0, 0}, VK_IMAGE_LAYOUT_UNDEFINED};
    static const ngfvk_sync_req sync_reqs[] = {
        {.barrier_masks =
             {.stage_mask  = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
              .access_mask = VK_ACCESS_SHADER_WRITE_BIT},
         .layout = VK_IMAGE_LAYOUT_GENERAL},
       {.barrier_masks =
             {.stage_mask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
              .access_mask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT},
         .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}
    };
    test_sync_req_merge(
        &dst_req,
        &sync_reqs[0],
        true,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_SHADER_WRITE_BIT,
        VK_IMAGE_LAYOUT_GENERAL);
    test_sync_req_merge(
        &dst_req,
        &sync_reqs[1],
        false,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_SHADER_WRITE_BIT,
        VK_IMAGE_LAYOUT_GENERAL);
  }


  NT_TESTCASE("sync req merge - write/read") {
    ngfvk_sync_req dst_req = {{0, 0}, VK_IMAGE_LAYOUT_UNDEFINED};
    static const ngfvk_sync_req sync_reqs[] = {
        {.barrier_masks =
             {.stage_mask  = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
              .access_mask = VK_ACCESS_SHADER_WRITE_BIT},
         .layout = VK_IMAGE_LAYOUT_GENERAL},
       {.barrier_masks =
             {.stage_mask  = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
              .access_mask = VK_ACCESS_SHADER_READ_BIT},
         .layout = VK_IMAGE_LAYOUT_GENERAL}
    };
    test_sync_req_merge(
        &dst_req,
        &sync_reqs[0],
        true,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_SHADER_WRITE_BIT,
        VK_IMAGE_LAYOUT_GENERAL);
    test_sync_req_merge(
        &dst_req,
        &sync_reqs[1],
        false,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_SHADER_WRITE_BIT,
        VK_IMAGE_LAYOUT_GENERAL);
  }


  NT_TESTCASE("sync req merge - read/write") {
    ngfvk_sync_req dst_req = {{0, 0}, VK_IMAGE_LAYOUT_UNDEFINED};
    static const ngfvk_sync_req sync_reqs[] = {
       {.barrier_masks =
             {.stage_mask  = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
              .access_mask = VK_ACCESS_SHADER_READ_BIT},
         .layout = VK_IMAGE_LAYOUT_GENERAL},
       {.barrier_masks =
             {.stage_mask  = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
              .access_mask = VK_ACCESS_SHADER_WRITE_BIT},
         .layout = VK_IMAGE_LAYOUT_GENERAL}
    };
    test_sync_req_merge(
        &dst_req,
        &sync_reqs[0],
        true,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_SHADER_READ_BIT,
        VK_IMAGE_LAYOUT_GENERAL);
    test_sync_req_merge(
        &dst_req,
        &sync_reqs[1],
        false,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_SHADER_READ_BIT,
        VK_IMAGE_LAYOUT_GENERAL);
  }

  NT_TESTCASE("sync req merge - layout change") {
    ngfvk_sync_req dst_req = {{0, 0}, VK_IMAGE_LAYOUT_UNDEFINED};
    static const ngfvk_sync_req sync_reqs[] = {
       {.barrier_masks =
             {.stage_mask  = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
              .access_mask = VK_ACCESS_SHADER_READ_BIT},
         .layout = VK_IMAGE_LAYOUT_GENERAL},
       {.barrier_masks =
             {.stage_mask  = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
              .access_mask = VK_ACCESS_SHADER_READ_BIT},
         .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}
    };
    test_sync_req_merge(
        &dst_req,
        &sync_reqs[0],
        true,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_SHADER_READ_BIT,
        VK_IMAGE_LAYOUT_GENERAL);
    test_sync_req_merge(
        &dst_req,
        &sync_reqs[1],
        false,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_SHADER_READ_BIT,
        VK_IMAGE_LAYOUT_GENERAL);
  }

 }


