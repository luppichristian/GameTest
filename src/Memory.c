/*
 * Memory.c - Memory management subsystem.
 *
 * GMT_Alloc_/GMT_Free_/GMT_Realloc_ route through the user-supplied callbacks
 * (from GMT_Setup), or fall back to C standard malloc/free/realloc.
 */

#include "Internal.h"
#include <stdlib.h>

void* GMT_Alloc_(size_t size, GMT_CodeLocation loc) {
  GMT_AllocCallback cb = g_gmt.setup.alloc_callback;
  if (cb) return cb(size, loc);
  (void)loc;
  return malloc(size);
}

void GMT_Free_(void* ptr, GMT_CodeLocation loc) {
  GMT_FreeCallback cb = g_gmt.setup.free_callback;
  if (cb) {
    cb(ptr, loc);
    return;
  }
  (void)loc;
  free(ptr);
}

void* GMT_Realloc_(void* ptr, size_t new_size, GMT_CodeLocation loc) {
  GMT_ReallocCallback cb = g_gmt.setup.realloc_callback;
  if (cb) return cb(ptr, new_size, loc);
  (void)loc;
  return realloc(ptr, new_size);
}
