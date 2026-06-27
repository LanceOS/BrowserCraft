#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

using Catch::Approx;
#include <glm/gtc/matrix_transform.hpp>
#include "engine/math/Frustum.hpp"

TEST_CASE("Frustum extractFrom populates 6 planes", "[math]") {
  using namespace terrain;
  auto proj = glm::perspective(glm::radians(70.0f), 16.0f / 9.0f, 0.1f, 1000.0f);
  auto view = glm::lookAt(
    glm::vec3(0.0f, 100.0f, 200.0f),
    glm::vec3(0.0f, 60.0f, 0.0f),
    glm::vec3(0.0f, 1.0f, 0.0f)
  );
  auto vp = proj * view;

  Frustum f;
  f.extractFrom(vp);

  // All 6 planes should be normalized (length ≈ 1)
  for (const auto& p : f.planes()) {
    float len = glm::length(glm::vec3(p));
    REQUIRE(len == Approx(1.0f).margin(0.01f));
  }
}

TEST_CASE("Frustum intersectsAABB with box inside view", "[math]") {
  using namespace terrain;
  auto proj = glm::perspective(glm::radians(70.0f), 16.0f / 9.0f, 0.1f, 1000.0f);
  auto view = glm::lookAt(
    glm::vec3(0.0f, 100.0f, 200.0f),
    glm::vec3(0.0f, 64.0f, 0.0f),
    glm::vec3(0.0f, 1.0f, 0.0f)
  );
  auto vp = proj * view;

  Frustum f;
  f.extractFrom(vp);

  // A chunk directly in front of the camera should be visible
  REQUIRE(f.intersectsAABB(-8.0f, 0.0f, -8.0f, 8.0f, 128.0f, 8.0f) == true);
}

TEST_CASE("Frustum intersectsAABB with box behind camera", "[math]") {
  using namespace terrain;
  auto proj = glm::perspective(glm::radians(70.0f), 16.0f / 9.0f, 0.1f, 1000.0f);
  auto view = glm::lookAt(
    glm::vec3(0.0f, 100.0f, 200.0f),
    glm::vec3(0.0f, 64.0f, 0.0f),
    glm::vec3(0.0f, 1.0f, 0.0f)
  );
  auto vp = proj * view;

  Frustum f;
  f.extractFrom(vp);

  // A chunk far behind the camera should be culled
  REQUIRE(f.intersectsAABB(-8.0f, 0.0f, 500.0f, 8.0f, 128.0f, 516.0f) == false);
}
