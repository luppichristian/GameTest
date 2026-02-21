/*
 * InputState.c - GMT_InputState utility implementation.
 */

#include "InputState.h"
#include <stdbool.h>
#include <string.h>

void GMT_InputState_Clear(GMT_InputState* s) {
  memset(s, 0, sizeof(*s));
}

bool GMT_InputState_Compare(const GMT_InputState* a, const GMT_InputState* b) {
  return memcmp(a, b, sizeof(*a)) == 0;
}
