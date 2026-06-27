#pragma once

#include <cstdint>

namespace terrain {

class InputState;
class World;
struct CameraView;
class UIManager;
class GameSession;

/// Per-frame context passed to ECS systems instead of a direct Game reference.
struct TickContext {
  InputState& input;
  World& world;
  CameraView& camera;
  UIManager& ui;
  GameSession& session;

  /// The player's entity ID (set each frame by Game).
  int32_t playerEntityId = 0;

  /// Flag: set to true when camera needs matrix recomputation.
  bool& cameraDirty;

  /// Frame delta time in seconds.
  float dt = 0.0f;
};

} // namespace terrain
