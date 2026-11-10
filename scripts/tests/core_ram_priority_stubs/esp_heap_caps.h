#pragma once
#include <stddef.h>
#include <stdint.h>

constexpr uint32_t MALLOC_CAP_INTERNAL = 1;
constexpr uint32_t MALLOC_CAP_SPIRAM = 2;
constexpr uint32_t MALLOC_CAP_8BIT = 4;

void* heap_caps_malloc(size_t bytes, uint32_t caps);
void heap_caps_free(void* memory);
