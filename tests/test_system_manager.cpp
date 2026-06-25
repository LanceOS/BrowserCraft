#include <catch2/catch_test_macros.hpp>
#include "engine/ecs/SystemManager.hpp"
#include "engine/core/TickContext.hpp"
#include "engine/core/InputState.hpp"
#include "engine/render/CameraView.hpp"
#include "game/GameSession.hpp"

namespace {
  class TestSystem final : public voxel::System {
    std::string m_name = "test";
  public:
    auto name() const -> const std::string& override { return m_name; }
    auto stage() const -> voxel::SystemStage override { return voxel::SystemStage::Physics; }
    void update(voxel::TickContext& ctx) override {
      ctx.playerEntityId += 1;
    }
  };

  class PreSystem final : public voxel::System {
    std::string m_name = "pre";
  public:
    auto name() const -> const std::string& override { return m_name; }
    auto stage() const -> voxel::SystemStage override { return voxel::SystemStage::PrePhysics; }
    void update(voxel::TickContext& ctx) override {
      ctx.playerEntityId *= 2;
    }
  };
}

TEST_CASE("SystemManager runs systems in stage order", "[ecs]") {
  voxel::SystemManager mgr;
  voxel::InputState input;
  bool dirty = false;
  voxel::CameraView camera;
  voxel::GameSession session(4);

  // Construct TickContext with valid references. World and UIManager
  // are never accessed by these test systems (only playerEntityId is touched).
  // We provide valid-but-unrelated stack objects to satisfy the reference members.
  // In a real game these would be the actual World and UIManager instances.
  struct Fake { int _; } dummyWorld, dummyUI;

  voxel::TickContext ctx{
    .input = input,
    .world = reinterpret_cast<voxel::World&>(dummyWorld),
    .camera = camera,
    .ui = reinterpret_cast<voxel::UIManager&>(dummyUI),
    .session = session,
    .playerEntityId = 42,
    .cameraDirty = dirty,
    .dt = 0.016f,
  };

  mgr.add(std::make_unique<TestSystem>());
  mgr.add(std::make_unique<PreSystem>());

  mgr.update(ctx);

  // PrePhysics runs first (42 *= 2 → 84), then Physics (84 += 1 → 85)
  REQUIRE(ctx.playerEntityId == 85);
}

TEST_CASE("SystemManager empty update is safe", "[ecs]") {
  voxel::SystemManager mgr;
  voxel::InputState input;
  bool dirty = false;
  voxel::CameraView camera;
  voxel::GameSession session(4);
  struct Fake { int _; } dummyWorld, dummyUI;

  voxel::TickContext ctx{
    .input = input,
    .world = reinterpret_cast<voxel::World&>(dummyWorld),
    .camera = camera,
    .ui = reinterpret_cast<voxel::UIManager&>(dummyUI),
    .session = session,
    .playerEntityId = 0,
    .cameraDirty = dirty,
    .dt = 0.016f,
  };
  mgr.update(ctx);
  REQUIRE(ctx.playerEntityId == 0);
}
