#pragma once

#include <array>
#include <cstdint>

namespace voxel {
namespace mesher {

/**
 * Precomputed AO Lookup Table.
 * 
 * We pack the three boolean states (side1, side2, corner) into a 3-bit integer (0-7)
 * and use a precomputed uint8_t LUT. This eliminates branching in the hot meshing loop.
 * 
 * Complexity: O(1) with zero branches.
 */
constexpr std::array<uint8_t, 8> generateAOLUT() {
    std::array<uint8_t, 8> lut{};
    for (int i = 0; i < 8; i++) {
        int s1 = (i >> 2) & 1;
        int s2 = (i >> 1) & 1;
        int c  = (i >> 0) & 1;
        if (s1 && s2) {
            lut[i] = 0;
        } else {
            lut[i] = 3 - (s1 + s2 + c);
        }
    }
    return lut;
}

constexpr auto AO_LUT = generateAOLUT();

inline uint8_t calculateAO(bool s1, bool s2, bool c) {
    // Pack into 3 bits: [s1, s2, c] -> index 0..7
    int index = ((s1 & 1) << 2) | ((s2 & 1) << 1) | (c & 1);
    return AO_LUT[index];
}

} // namespace mesher
} // namespace voxel
