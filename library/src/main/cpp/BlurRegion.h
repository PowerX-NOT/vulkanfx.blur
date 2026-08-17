#pragma once

#include <cstdint>

// Matches frameworks/native/libs/ui/include/ui/BlurRegion.h
struct BlurRegion {
    uint32_t blurRadius = 0;
    float cornerRadiusTL = 0.f;
    float cornerRadiusTR = 0.f;
    float cornerRadiusBL = 0.f;
    float cornerRadiusBR = 0.f;
    float alpha = 1.f;
    int32_t left = 0;
    int32_t top = 0;
    int32_t right = 0;
    int32_t bottom = 0;
};
