#include <catch2/catch_test_macros.hpp>
#include "engine/ecs/SystemManager.hpp"

namespace {
  struct TestState {
    int counter = 0;
  };

  class TestSystem final : public voxel::System<TestState> {
    std::string m_name = "test";
  public:
    auto name() const -> const std::string& override { return m_name; }
    auto stage() const -> voxel::SystemStage override { return voxel::SystemStage::Physics; }
    void update(TestState& state, float) override { state.counter += 1; }
  };

  class PreSystem final : public voxel::System<TestState> {
    std::string m_name = "pre";
  public:
    auto name() const -> const std::string& override { return m_name; }
    auto stage() const -> voxel::SystemStage override { return voxel::SystemStage::PrePhysics; }
    void update(TestState& state, float) override { state.counter *= 2; }
  };
}

TEST_CASE("SystemManager runs systems in stage order", "[ecs]") {
  voxel::SystemManager<TestState> mgr;
  TestState state;

  mgr.add(std::make_unique<TestSystem>());
  mgr.add(std::make_unique<PreSystem>());

  mgr.update(state, 0.016f);

  // PrePhysics runs first (counter *= 2 → 0), then Physics (counter += 1 → 1)
  REQUIRE(state.counter == 1);
}

TEST_CASE("SystemManager empty update is safe", "[ecs]") {
  voxel::SystemManager<TestState> mgr;
  TestState state;
  mgr.update(state, 0.016f);
  REQUIRE(state.counter == 0);
}
