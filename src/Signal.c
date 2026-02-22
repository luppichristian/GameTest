/*
MIT License

Copyright (c) 2026 Christian Luppi

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
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
      if (g_gmt.replay_signal_cursor >= g_gmt.replay_signal_count) {
        GMT_LogWarning("GMT_SyncSignal: signal id %d has no corresponding recorded entry (all %zu recorded signals already consumed); ignored.",
                       id,
                       g_gmt.replay_signal_count);

      } else if (g_gmt.replay_signals[g_gmt.replay_signal_cursor].signal_id != id) {
        GMT_LogWarning("GMT_SyncSignal: signal id %d does not match next expected id %d at cursor %zu; ignored.",
                       id,
                       g_gmt.replay_signals[g_gmt.replay_signal_cursor].signal_id,
                       g_gmt.replay_signal_cursor);
      } else {
        double now = GMT_Platform_GetTime();
        double st = g_gmt.replay_signals[g_gmt.replay_signal_cursor].timestamp;
        if (g_gmt.waiting_for_signal && g_gmt.waiting_signal_id == id) {
          // Normal (late) case: the injection gate was set because the replay engine
          // already reached the signal's timestamp, and the game is now catching up.
          // Offset by how long we waited so subsequent timestamps stay consistent.
          g_gmt.replay_time_offset += (now - g_gmt.signal_wait_start);
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
