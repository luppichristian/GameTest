/*
 * InputState.c - GMT_InputState utility implementation.
 */

#include "InputState.h"
#include <string.h>

void GMT_InputState_Clear(GMT_InputState* s) {
  memset(s, 0, sizeof(*s));
}
