#pragma once
#include "../storage_stubs/esp_heap_caps.h"
namespace fileUiHeap {
inline auto& allocations=host_heap::psramBlocks;
inline auto& limit=host_heap::psramBudget;
inline size_t internalFree=128*1024,internalLargest=96*1024,psramFree=128*1024,psramLargest=96*1024;
inline bool hasAllocation(size_t size){for(const auto& p:allocations)if(p.second==size)return true;return false;}
}
inline size_t heap_caps_get_free_size(int caps){return caps&MALLOC_CAP_SPIRAM?fileUiHeap::psramFree:fileUiHeap::internalFree;}
inline size_t heap_caps_get_minimum_free_size(int){return 96*1024;}
inline size_t heap_caps_get_largest_free_block(int caps){return caps&MALLOC_CAP_SPIRAM?fileUiHeap::psramLargest:fileUiHeap::internalLargest;}
