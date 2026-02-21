/*
 * Signal.c - Sync signal implementation.
 *
 * In RECORD mode  : writes a TAG_SIGNAL record to the test file at the current timestamp.
 * In REPLAY mode  : advances the signal cursor and adjusts the replay clock whenever
 *                   the game emits the next expected signal.  Two cases:
 *                    - Normal/late: the injection gate was already set (waiting_for_signal),
 *                      meaning the replay engine reached the signal timestamp before the
 *                      game emitted it.  The offset is increased by the wait duration so
 *                      subsequent timestamps remain consistent.
 *                    - Early: the game fired the signal before the replay engine reached
 *                      its recorded timestamp (e.g. an "Init" signal before the main loop).
 *                      The offset is adjusted so that replay_time equals the signal's
 *                      recorded timestamp going forward, keeping input timing correct.
 * In DISABLED mode: no-op.
 *
 * The optional user signal callback (GMT_Setup::signal_callback) is invoked in all modes.
 */

#include "Internal.h"
#include "Record.h"

void GMT_SyncSignal_(int id, GMT_CodeLocation loc) {
  if (!g_gmt.initialized) return;
  if (g_gmt.mode == GMT_Mode_DISABLED) return;

  GMT_LogInfo("Signal sync id %d triggered at %s:%s:%d", id, loc.file, loc.function, loc.line);
  GMT_Platform_MutexLock();

  switch (g_gmt.mode) {
    case GMT_Mode_RECORD:
      GMT_Record_WriteSignal((int32_t)id);
      break;

    case GMT_Mode_REPLAY:
      // Advance the signal cursor whenever the game emits the next expected signal,
      // regardless of whether the injection gate has been set yet.  This handles
      // signals that fire before the first GMT_Update call (e.g. an "Init" signal
      // placed before the main loop), where waiting_for_signal would never be set
      // when the game fires it, causing a permanent deadlock.
      if (g_gmt.replay_signal_cursor < g_gmt.replay_signal_count &&
          g_gmt.replay_signals[g_gmt.replay_signal_cursor].signal_id == id) {
        double now = GMT_Platform_GetTime();
        double st = g_gmt.replay_signals[g_gmt.replay_signal_cursor].timestamp;
        if (g_gmt.waiting_for_signal && g_gmt.waiting_signal_id == id) {
          // Normal (late) case: the injection gate was set because the replay engine
          // already reached the signal's timestamp, and the game is now catching up.
          // Offset by how long we waited so subsequent timestamps stay consistent.
          g_gmt.replay_time_offset += (now - g_gmt.signal_wait_start);
          g_gmt.waiting_for_signal = false;
        } else {
          // Early case: game fired the signal before replay reached its recorded
          // timestamp (e.g. an "Init" signal before the main loop).  Align the
          // replay clock so that replay_time == st going forward, ensuring subsequent
          // input records inject at the correct time relative to this sync point.
          double replay_time_now = (now - g_gmt.record_start_time) - g_gmt.replay_time_offset;
          g_gmt.replay_time_offset += (replay_time_now - st);
        }
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
