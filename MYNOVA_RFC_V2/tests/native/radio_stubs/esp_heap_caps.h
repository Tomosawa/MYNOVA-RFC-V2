#pragma once
#include <cstdlib>
#include <cassert>
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_SPIRAM 4
inline bool failRmtAlloc = false, failPsramAlloc = false;
inline void* heap_caps_malloc(size_t bytes, int caps) {
    assert(caps == (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) || caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    return failRmtAlloc || ((caps & MALLOC_CAP_SPIRAM) && failPsramAlloc) ? nullptr : std::malloc(bytes);
}
inline void* heap_caps_calloc(size_t count,size_t bytes,int caps) {
    void* p=heap_caps_malloc(count*bytes,caps);
    if(p) std::memset(p,0,count*bytes);
    return p;
}
inline void heap_caps_free(void* p) {std::free(p);}
