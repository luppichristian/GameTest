/*
 * GameTest.c - Core framework: global state, lifecycle (Init/Quit), and runtime (Update/Reset/Fail).
 *
 * This file owns the single instance of GMT_State (g_gmt) and orchestrates all subsystems.
 */

#include "Internal.h"
#include "Record.h"
#include <inttypes.h>
#include <string.h>

// ===== Global state =====

GMT_State g_gmt;

// ===== Default fail callback =====

static void GMT_DefaultFail(void) {
  GMT_LogError("Test FAILED.  Exiting.");
  GMT_PrintReport_();
  // Use abort instead of exit so that a debugger can catch it.
  // In CI the process will terminate with a non-zero code either way.
  abort();
}

// ===== Init / Quit =====

bool GMT_Init_(const GMT_Setup* setup) {
  if (!setup) {
    GMT_LogError("Cant call GMT_Init with a null setup pointer.");
    return false;
  }
  if (g_gmt.initialized) {
    GMT_LogWarning("Already initialized; call GMT_Quit() first.");
    return false;
  }

  // Zero the state before populating it.
  memset(&g_gmt, 0, sizeof(g_gmt));

  // Shallow-copy the setup first so we know the mode before touching the platform.
  // The caller is responsible for keeping any strings and arrays alive.
  g_gmt.setup = *setup;
  g_gmt.mode = setup->mode;

  {
    const char* mode_str = (g_gmt.mode == GMT_Mode_RECORD)   ? "RECORD"
                           : (g_gmt.mode == GMT_Mode_REPLAY) ? "REPLAY"
                                                             : "DISABLED";
    GMT_LogInfo("Running GameTest with the following setup:");
    GMT_LogInfo("  Mode:                      %s", mode_str);
    GMT_LogInfo("  Test Path:                 %s", setup->test_path ? setup->test_path : "(null)");
    GMT_LogInfo("  Work Dir:                  %s", setup->work_dir ? setup->work_dir : "(null)");
    GMT_LogInfo("  Directory Mapping Count:   %zu", setup->directory_mapping_count);
    GMT_LogInfo("  Fail Assert Trigger Count: %d", setup->fail_assertion_trigger_count);
    GMT_LogInfo("  Log Callback:              %s", setup->log_callback ? "set" : "null");
    GMT_LogInfo("  Alloc Callback:            %s", setup->alloc_callback ? "set" : "null");
    GMT_LogInfo("  Free Callback:             %s", setup->free_callback ? "set" : "null");
    GMT_LogInfo("  Realloc Callback:          %s", setup->realloc_callback ? "set" : "null");
    GMT_LogInfo("  Signal Callback:           %s", setup->signal_callback ? "set" : "null");
    GMT_LogInfo("  Fail Callback:             %s", setup->fail_callback ? "set" : "null");
    GMT_LogInfo("  Assert Trigger Callback:   %s", setup->assertion_trigger_callback ? "set" : "null");
  }

  // In DISABLED mode skip all platform hooks and timers.
  if (g_gmt.mode == GMT_Mode_DISABLED) {
    g_gmt.initialized = true;
    return true;
  }

  // Install platform input hooks (e.g. mouse wheel accumulator).
  GMT_Platform_Init();

  // Optional working directory.
  if (setup->work_dir && setup->work_dir[0] != '\0') {
    GMT_Platform_SetWorkDir(setup->work_dir);
  }

  // Mode-specific initialisation.
  switch (g_gmt.mode) {
    case GMT_Mode_RECORD:
      if (!GMT_Record_OpenForWrite()) {
        GMT_LogError("Failed to open test file for recording");
        memset(&g_gmt, 0, sizeof(g_gmt));
        return false;
      }
      GMT_LogInfo("Test file opened for recording");
      break;

    case GMT_Mode_REPLAY:
      if (!GMT_Record_LoadReplay()) {
        GMT_LogError("Failed to load test file for replay");
        memset(&g_gmt, 0, sizeof(g_gmt));
        return false;
      }
      GMT_LogInfo("Test file loaded for replay");
      {
        GMT_FileMetrics m = GMT_Record_GetReplayMetrics();
        GMT_LogInfo("  Replay input records:  %zu", m.input_count);
        GMT_LogInfo("  Replay signal records: %zu", m.signal_count);
        GMT_LogInfo("  Recording length:      %.2f s", m.duration);
        GMT_LogInfo("  Input density:         %.2f records/s", m.input_density);
      }

      // Install IAT hooks to intercept all Win32 input-polling functions.
      GMT_Platform_InstallInputHooks();
      GMT_LogInfo("Input hooks installed");
      break;

    case GMT_Mode_DISABLED:
    default:
      break;
  }

  g_gmt.initialized = true;

  // Start the recording / replay clock.
  g_gmt.record_start_time = GMT_Platform_GetTime();
  g_gmt.replay_time_offset = 0.0;
  g_gmt.signal_wait_start = 0.0;

  // Activate replayed-state hooks now that the clock is running.
  if (g_gmt.mode == GMT_Mode_REPLAY) {
    GMT_Platform_SetReplayHooksActive(true);
  }

  return true;
}

void GMT_Quit_(void) {
  if (!g_gmt.initialized) return;
  if (g_gmt.mode == GMT_Mode_DISABLED) {
    memset(&g_gmt, 0, sizeof(g_gmt));
    return;
  }

  // Deactivate hooks before tearing down.
  GMT_Platform_SetReplayHooksActive(false);

  // Finalise recording / replay.
  switch (g_gmt.mode) {
    case GMT_Mode_RECORD: {
      GMT_FileMetrics m = GMT_Record_GetRecordMetrics();
      GMT_LogInfo("Closing recording file");
      GMT_LogInfo("  File size:     %ld bytes", m.file_size_bytes);
      GMT_LogInfo("  Duration:      %.2f s", m.duration);
      GMT_LogInfo("  Frames:        %" PRIu64, m.frame_count);
      GMT_LogInfo("  Input records: %zu", m.input_count);
      GMT_LogInfo("  Signal records: %zu", m.signal_count);
      GMT_LogInfo("  Input density: %.2f records/s", m.input_density);
      GMT_Record_CloseWrite();
      break;
    }
    case GMT_Mode_REPLAY:
      GMT_LogInfo("Freeing and stopping replay");
      GMT_Record_FreeReplay();
      break;
    default:
      break;
  }

  GMT_PrintReport_();

  GMT_Platform_Quit();
  memset(&g_gmt, 0, sizeof(g_gmt));
}

// ===== Runtime =====

void GMT_Update_(void) {
  if (!g_gmt.initialized) return;
  if (g_gmt.mode == GMT_Mode_DISABLED) return;

  GMT_Platform_MutexLock();

  // Reset per-frame sequential key counters for Pin and Track.
  GMT_KeyCounter_Reset(&g_gmt.pin_counter);
  GMT_KeyCounter_Reset(&g_gmt.track_counter);

  switch (g_gmt.mode) {
    case GMT_Mode_RECORD:
      GMT_Record_WriteInput();
      break;
    case GMT_Mode_REPLAY:
      GMT_Record_InjectInput();
      break;
    default:
      break;
  }

  g_gmt.frame_index++;

  GMT_Platform_MutexUnlock();
}

void GMT_Reset_(void) {
  if (!g_gmt.initialized) return;
  if (g_gmt.mode == GMT_Mode_DISABLED) return;

  GMT_Platform_MutexLock();
  GMT_LogInfo("Resetting session (frame_index was %u).", g_gmt.frame_index);

  // Tear down the current recording / replay session.
  switch (g_gmt.mode) {
    case GMT_Mode_RECORD:
      // Close the current file (writes TAG_END) and start a new one.
      GMT_Record_CloseWrite();
      // Reopen; any existing data is overwritten.
      GMT_Record_OpenForWrite();
      GMT_LogInfo("Recording file reopened");
      break;

    case GMT_Mode_REPLAY:
      GMT_Record_FreeReplay();
      GMT_Record_LoadReplay();
      GMT_LogInfo("Replay data reloaded");
      break;

    default:
      break;
  }

  // Reset runtime statistics.
  g_gmt.frame_index = 0;
  g_gmt.test_failed = false;
  g_gmt.failed_assertion_count = 0;
  g_gmt.assertion_fire_count = 0;
  g_gmt.waiting_for_signal = false;
  g_gmt.waiting_signal_id = 0;
  GMT_InputState_Clear(&g_gmt.replay_prev_input);
  GMT_KeyCounter_Reset(&g_gmt.pin_counter);
  GMT_KeyCounter_Reset(&g_gmt.track_counter);

  // Reset the recording / replay clock.
  g_gmt.record_start_time = GMT_Platform_GetTime();
  g_gmt.replay_time_offset = 0.0;
  g_gmt.signal_wait_start = 0.0;

  GMT_Platform_MutexUnlock();
}

void GMT_Fail_(void) {
  if (!g_gmt.initialized) return;
  if (g_gmt.mode == GMT_Mode_DISABLED) return;

  GMT_Platform_MutexLock();
  g_gmt.test_failed = true;
  GMT_LogError("Test marked as failed on frame %u.", g_gmt.frame_index);
  GMT_Platform_MutexUnlock();

  // Remove input-blocking hooks before invoking any callback that may open a
  // dialog.  During replay the LL hooks swallow all real keyboard and mouse
  // events; without this the user cannot interact with (or dismiss) error
  // dialogs such as the OS crash prompt from abort().
  GMT_Platform_RemoveInputHooks();

  // Dereference pointer-to-function-pointer pattern used for overridable callbacks.
  if (g_gmt.setup.fail_callback && *g_gmt.setup.fail_callback) {
    GMT_FailCallback cb = *g_gmt.setup.fail_callback;
    cb();
  } else {
    GMT_DefaultFail();
  }
}
