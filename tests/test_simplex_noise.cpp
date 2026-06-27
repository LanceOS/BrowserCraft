#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "world/generation/SimplexNoise.hpp"

using Catch::Approx;

TEST_CASE("SimplexNoise produces values in range [-1,1]", "[noise]") {
  terrain::SimplexNoise noise(42);

  float minVal = 999.0f, maxVal = -999.0f;
  for (float x = 0; x < 10.0f; x += 0.5f) {
    for (float z = 0; z < 10.0f; z += 0.5f) {
      float val = noise.noise2D(x, z);
      minVal = std::min(minVal, val);
      maxVal = std::max(maxVal, val);
    }
  }
  REQUIRE(minVal >= -1.5f);
  REQUIRE(maxVal <= 1.5f);
}

TEST_CASE("SimplexNoise is deterministic", "[noise]") {
  terrain::SimplexNoise a(12345);
  terrain::SimplexNoise b(12345);

  for (int i = 0; i < 20; ++i) {
    float x = i * 0.5f;
    float z = i * 0.3f;
    REQUIRE(a.noise2D(x, z) == Approx(b.noise2D(x, z)).margin(0.0001f));
  }
}

TEST_CASE("SimplexNoise different seeds produce different values", "[noise]") {
  terrain::SimplexNoise a(1);
  terrain::SimplexNoise b(9999);

  int different = 0;
  for (int i = 0; i < 20; ++i) {
    if (std::abs(a.noise2D(i * 0.5f, 0.0f) - b.noise2D(i * 0.5f, 0.0f)) > 0.01f) {
      ++different;
    }
  }
  REQUIRE(different > 10); // most should differ
}

TEST_CASE("SimplexNoise 3D produces values in range", "[noise]") {
  terrain::SimplexNoise noise(7);

  float minVal = 999.0f, maxVal = -999.0f;
  for (float x = 0; x < 5.0f; x += 1.0f) {
    for (float y = 0; y < 5.0f; y += 1.0f) {
      for (float z = 0; z < 5.0f; z += 1.0f) {
        float val = noise.noise3D(x, y, z);
        minVal = std::min(minVal, val);
        maxVal = std::max(maxVal, val);
      }
    }
  }
  REQUIRE(minVal >= -1.5f);
  REQUIRE(maxVal <= 1.5f);
}
