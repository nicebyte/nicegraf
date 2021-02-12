#include "catch.hpp"
#include "ngf-common/frame_token.h"

TEST_CASE("encode-decode frame token works") {
  const uint16_t  test_ctx_id = 65534u, test_max_inflight_frames = 3u, test_frame_id = 255u;
  const uint32_t test_token =
      ngfi_encode_frame_token(test_ctx_id, test_max_inflight_frames, test_frame_id);
  REQUIRE(test_ctx_id == ngfi_frame_ctx_id(test_token));
  REQUIRE(test_max_inflight_frames == ngfi_frame_max_inflight_frames(test_token));
  REQUIRE(test_frame_id == ngfi_frame_id(test_token));
}


