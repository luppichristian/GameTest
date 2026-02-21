/*
 * Signal.c - Sync signal implementation.
 *
 * In RECORD mode  : writes a TAG_SIGNAL record to the test file at the current timestamp.
 * In REPLAY mode  : if the framework is currently blocked waiting for this signal id,
 *                   it unblocks injection, advances the signal cursor, and offsets the
 *                   replay clock by the time spent waiting so that subsequent timestamps
 *                   remain consistent.
 * In DISABLED mode: no-op.
 *
 * The optional user signal callback (GMT_Setup::signal_callback) is invoked in all modes.
 */

#include "Internal.h"
#include "Record.h"

void GMT_SyncSignal_(int id, GMT_CodeLocation loc) {
  if (!g_gmt.initialized) return;

  GMT_Platform_MutexLock();

  switch (g_gmt.mode) {
    case GMT_Mode_RECORD:
      GMT_Record_WriteSignal((int32_t)id);
      break;

    case GMT_Mode_REPLAY:
      // If we're waiting for exactly this signal id, unblock injection.
      if (g_gmt.waiting_for_signal && g_gmt.waiting_signal_id == id) {
        double now = GMT_Platform_GetTime();
        // Offset replay time by the duration of the wait so that frames
        // after this signal play back at the correct relative timing.
        g_gmt.replay_time_offset += (now - g_gmt.signal_wait_start);
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
    GMT_Platform_MutexUnlock();
    cb(g_gmt.mode, id, loc);
    return;
  }

  GMT_Platform_MutexUnlock();
}
