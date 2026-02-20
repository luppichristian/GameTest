/*
 * Signal.c - Sync signal implementation.
 *
 * In RECORD mode  : writes a TAG_SIGNAL record to the test file at the current frame.
 * In REPLAY mode  : if the framework is currently blocked waiting for this signal id,
 *                   it unblocks injection and advances the signal cursor.
 * In DISABLED mode: no-op.
 *
 * The optional user signal callback (GMT_Setup::signal_callback) is invoked in all modes.
 */

#include "Internal.h"
#include "Record.h"

void GMT_SyncSignal_(int id, GMT_CodeLocation loc) {
  if (!g_gmt.initialized) return;

  GMT_Platform_MutexLock(&g_gmt.mutex);

  switch (g_gmt.mode) {
    case GMT_Mode_RECORD:
      GMT_Record_WriteSignal((int32_t)id, g_gmt.frame_index);
      break;

    case GMT_Mode_REPLAY:
      // If we're waiting for exactly this signal id, unblock injection.
      if (g_gmt.waiting_for_signal && g_gmt.waiting_signal_id == id) {
        g_gmt.waiting_for_signal = false;
        g_gmt.replay_signal_cursor++;
      }
      break;

    case GMT_Mode_DISABLED:
    default:
      break;
  }

  // Fire user callback (pointer to function pointer; read through it).
  if (g_gmt.setup.signal_callback && *g_gmt.setup.signal_callback) {
    GMT_SignalCallback cb = *g_gmt.setup.signal_callback;
    GMT_Platform_MutexUnlock(&g_gmt.mutex);
    cb(g_gmt.mode, id, loc);
    return;
  }

  GMT_Platform_MutexUnlock(&g_gmt.mutex);
}
