#include "catch.hpp"
#include "stack_alloc.h"

TEST_CASE("exhaust-reset-exhaust", "[stack_alloc]") {
  const uint32_t value   = 0xdeadbeef;
  const uint32_t nvalues = 10;
  ngfi_sa *sa = ngfi_sa_create(sizeof(value) * nvalues);
  REQUIRE(sa != NULL);
  for (uint32_t i = 0; i < nvalues + 1; ++i) {
    uint32_t *target = (uint32_t*)ngfi_sa_alloc(sa, sizeof(value));
    if (i >= nvalues) REQUIRE(target == NULL);
    else {
      *target = value;
      REQUIRE(*target == value);
    }
  }
  ngfi_sa_reset(sa);
  for (uint32_t i = 0; i < nvalues + 1; ++i) {
    uint32_t *target = (uint32_t*)ngfi_sa_alloc(sa, sizeof(value));
    if (i >= nvalues) REQUIRE(target == NULL);
    else {
      *target = value;
      REQUIRE(*target == value);
    }
  }
  ngfi_sa_destroy(sa);
}
