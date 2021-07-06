#include "catch.hpp"
#include "ngf-common/native-binding-map.h"

namespace {

struct bind_ops_builder {
  bind_ops_builder(uint32_t set, uint32_t binding, ngf_descriptor_type type) {
    ngfi_list_init(&pending_list.pending_ops_list_node);
    pending_list.op.target_set = set;
    pending_list.op.target_binding = binding;
    pending_list.op.type = type;
  }

  bind_ops_builder& add(uint32_t set, uint32_t binding, ngf_descriptor_type t) {
    auto new_pending = new ngfi_pending_bind_op{};
    ngfi_list_init(&new_pending->pending_ops_list_node);
    new_pending->op.target_set = set;
    new_pending->op.target_binding = binding;
    new_pending->op.type = t;
    ngfi_list_append(&new_pending->pending_ops_list_node,
                     &pending_list.pending_ops_list_node);
    return *this;
  }

  ngfi_native_binding_map* build() {
    return 
    ngfi_create_native_binding_map_from_pending_bind_ops(&pending_list);
  }

  ngfi_pending_bind_op pending_list;
};

}

TEST_CASE("oob-ids") {
  bind_ops_builder builder { 0, 0, NGF_DESCRIPTOR_SAMPLER };
  ngfi_native_binding_map* map = builder.build();
  uint32_t oob_set_result = ngfi_native_binding_map_lookup(map, 1, 0),
    oob_binding_result = ngfi_native_binding_map_lookup(map, 0, 1),
    oob_set_and_binding_result = ngfi_native_binding_map_lookup(map, 1, 1),
    valid_set_and_binding_result = ngfi_native_binding_map_lookup(map, 0, 0);
  REQUIRE(valid_set_and_binding_result == 0);
  REQUIRE(oob_set_result == ~0);
  REQUIRE(oob_binding_result == ~0);
  REQUIRE(oob_set_and_binding_result == ~0);
  ngfi_destroy_native_binding_map(map);
}

TEST_CASE("different-desc-types") {
  bind_ops_builder builder { 0, 0, NGF_DESCRIPTOR_SAMPLER};
  builder.add(0, 1, NGF_DESCRIPTOR_TEXTURE);
  builder.add(0, 2, NGF_DESCRIPTOR_SAMPLER);
  ngfi_native_binding_map* map = builder.build();
  uint32_t sampler0_result = ngfi_native_binding_map_lookup(map, 0, 0),
           sampler1_result = ngfi_native_binding_map_lookup(map, 0, 2),
           texture0_result = ngfi_native_binding_map_lookup(map, 0, 1);
  REQUIRE(sampler0_result == 0);
  REQUIRE(sampler1_result == 1);
  REQUIRE(texture0_result == 0);
  ngfi_destroy_native_binding_map(map);
}

TEST_CASE("different-desc-types-across-sets") {
  bind_ops_builder builder { 0, 0, NGF_DESCRIPTOR_SAMPLER};
  builder.add(0, 1, NGF_DESCRIPTOR_TEXTURE);
  builder.add(1, 0, NGF_DESCRIPTOR_SAMPLER);
  ngfi_native_binding_map* map = builder.build();
  uint32_t sampler0_result = ngfi_native_binding_map_lookup(map, 0, 0),
           sampler1_result = ngfi_native_binding_map_lookup(map, 1, 0),
           texture0_result = ngfi_native_binding_map_lookup(map, 0, 1);
  REQUIRE(sampler0_result == 0);
  REQUIRE(sampler1_result == 1);
  REQUIRE(texture0_result == 0);
  ngfi_destroy_native_binding_map(map);
}

TEST_CASE("hole-in-sets") {
  bind_ops_builder builder { 0, 0, NGF_DESCRIPTOR_SAMPLER};
  builder.add(2, 0, NGF_DESCRIPTOR_UNIFORM_BUFFER);
  builder.add(0, 1, NGF_DESCRIPTOR_TEXTURE);
  builder.add(0, 2, NGF_DESCRIPTOR_SAMPLER);
  ngfi_native_binding_map* map = builder.build();
  uint32_t sampler0_result = ngfi_native_binding_map_lookup(map, 0, 0),
           sampler1_result = ngfi_native_binding_map_lookup(map, 0, 2),
           texture0_result = ngfi_native_binding_map_lookup(map, 0, 1),
           hole_result = ngfi_native_binding_map_lookup(map, 1, 0),
           ubuffer0_result = ngfi_native_binding_map_lookup(map, 2, 0);
  REQUIRE(sampler0_result == 0);
  REQUIRE(sampler1_result == 1);
  REQUIRE(texture0_result == 0);
  REQUIRE(hole_result == ~0);
  REQUIRE(ubuffer0_result == 0);
}

TEST_CASE("hole-in-bindings") {
  bind_ops_builder builder { 0, 0, NGF_DESCRIPTOR_TEXTURE };
  builder.add(0, 1, NGF_DESCRIPTOR_TEXTURE);
  builder.add(0, 4, NGF_DESCRIPTOR_TEXTURE);
  builder.add(0, 5, NGF_DESCRIPTOR_TEXTURE);
  ngfi_native_binding_map* map = builder.build();
  uint32_t texture0_result = ngfi_native_binding_map_lookup(map, 0, 0),
           texture1_result = ngfi_native_binding_map_lookup(map, 0, 1),
           hole0_result = ngfi_native_binding_map_lookup(map, 0, 2),
           hole1_result = ngfi_native_binding_map_lookup(map, 0, 3),
           texture2_result = ngfi_native_binding_map_lookup(map, 0, 4),
           texture3_result = ngfi_native_binding_map_lookup(map, 0, 5);
  REQUIRE(texture0_result == 0);
  REQUIRE(texture1_result == 1);
  REQUIRE(hole0_result == ~0);
  REQUIRE(hole1_result == ~0);
  REQUIRE(texture2_result == 2);
  REQUIRE(texture3_result == 3);
}

TEST_CASE("arbitrary-binding-order-with-holes") {
  bind_ops_builder builder { 3, 1, NGF_DESCRIPTOR_UNIFORM_BUFFER };
  builder.add(5, 0, NGF_DESCRIPTOR_TEXTURE);
  builder.add(0, 0, NGF_DESCRIPTOR_TEXTURE);
  builder.add(2, 1, NGF_DESCRIPTOR_UNIFORM_BUFFER);
  builder.add(2, 2, NGF_DESCRIPTOR_SAMPLER);
  builder.add(0, 2, NGF_DESCRIPTOR_SAMPLER);
  ngfi_native_binding_map* map = builder.build();
  uint32_t texture0_result = ngfi_native_binding_map_lookup(map, 0, 0),
           texture1_result = ngfi_native_binding_map_lookup(map, 5, 0),
           sampler0_result = ngfi_native_binding_map_lookup(map, 0, 2),
           sampler1_result = ngfi_native_binding_map_lookup(map, 2, 2),
           ubuffer0_result = ngfi_native_binding_map_lookup(map, 2, 1),
           ubuffer1_result = ngfi_native_binding_map_lookup(map, 3, 1),
           hole0_result = ngfi_native_binding_map_lookup(map, 4, 0),
           hole1_result = ngfi_native_binding_map_lookup(map, 3, 0),
           hole2_result = ngfi_native_binding_map_lookup(map, 2, 0);
  REQUIRE(texture0_result == 0);
  REQUIRE(texture1_result == 1);
  REQUIRE(sampler0_result == 0);
  REQUIRE(sampler1_result == 1);
  REQUIRE(ubuffer0_result == 0);
  REQUIRE(ubuffer1_result == 1);
  REQUIRE(hole0_result == ~0);
  REQUIRE(hole1_result == ~0);
  REQUIRE(hole2_result == ~0);
}
