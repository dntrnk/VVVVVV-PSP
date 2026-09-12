#include <SDL_stdinc.h>

#include "Alloc.h"

/* Handle third-party dependencies' needs here */

void* lodepng_malloc(size_t size)
{
    return malloc(size);
}

void* lodepng_realloc(void* ptr, size_t new_size)
{
    return realloc(ptr, new_size);
}

void lodepng_free(void* ptr)
{
    VVV_free(ptr);
}
