/*
 * GameTest.c - Core framework: global state, lifecycle (Init/Quit), and runtime (Update/Reset/Fail).
 *
 * This file owns the single instance of GMT_State (g_gmt) and orchestrates all subsystems.
 */

#include "Internal.h"
#include "Record.h"
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
    GMT_LogError("GMT_Init: NULL setup pointer; aborting.");
    return false;
  }
  if (g_gmt.initialized) {
    GMT_LogWarning("GMT_Init called while already initialized; call GMT_Quit first.");
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
    GMT_LogInfo("GMT_Init: Running with following setup:");
    GMT_LogInfo("  mode                       = %s", mode_str);
    GMT_LogInfo("  test_path                  = %s", setup->test_path ? setup->test_path : "(null)");
    GMT_LogInfo("  work_dir                   = %s", setup->work_dir ? setup->work_dir : "(null)");
    GMT_LogInfo("  directory_mapping_count    = %zu", setup->directory_mapping_count);
    GMT_LogInfo("  fail_assertion_trigger_count = %d", setup->fail_assertion_trigger_count);
    GMT_LogInfo("  log_callback               = %s", setup->log_callback ? "set" : "null");
    GMT_LogInfo("  alloc_callback             = %s", setup->alloc_callback ? "set" : "null");
    GMT_LogInfo("  free_callback              = %s", setup->free_callback ? "set" : "null");
    GMT_LogInfo("  realloc_callback           = %s", setup->realloc_callback ? "set" : "null");
    GMT_LogInfo("  signal_callback            = %s", setup->signal_callback ? "set" : "null");
    GMT_LogInfo("  fail_callback              = %s", setup->fail_callback ? "set" : "null");
    GMT_LogInfo("  assertion_trigger_callback = %s", setup->assertion_trigger_callback ? "set" : "null");
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
        GMT_LogError("GMT_Init: failed to open test file for recording.");
        memset(&g_gmt, 0, sizeof(g_gmt));
        return false;
      }
      GMT_LogInfo("GMT_Init: test file opened for recording.");
      break;

    case GMT_Mode_REPLAY:
      if (!GMT_Record_LoadReplay()) {
        GMT_LogError("GMT_Init: failed to load test file for replay.");
        memset(&g_gmt, 0, sizeof(g_gmt));
        return false;
      }
      GMT_LogInfo("GMT_Init: test file loaded for replay.");
      // Install IAT hooks to intercept all Win32 input-polling functions.
      GMT_Platform_InstallInputHooks();
      GMT_LogInfo("GMT_Init: input hooks installed.");
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
  GMT_LogInfo("GMT_Quit: shutting down.");

  if (g_gmt.mode == GMT_Mode_DISABLED) {
    memset(&g_gmt, 0, sizeof(g_gmt));
    return;
  }

  // Deactivate hooks before tearing down.
  GMT_Platform_SetReplayHooksActive(false);

  // Finalise recording / replay.
  switch (g_gmt.mode) {
    case GMT_Mode_RECORD:
      GMT_LogInfo("GMT_Quit: closing recording file.");
      GMT_Record_CloseWrite();
      break;
    case GMT_Mode_REPLAY:
      GMT_LogInfo("GMT_Quit: freeing replay data.");
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
  GMT_LogInfo("GMT_Reset: resetting session (frame_index was %u).", g_gmt.frame_index);

  // Tear down the current recording / replay session.
  switch (g_gmt.mode) {
    case GMT_Mode_RECORD:
      // Close the current file (writes TAG_END) and start a new one.
      GMT_Record_CloseWrite();
      // Reopen; any existing data is overwritten.
      GMT_Record_OpenForWrite();
      GMT_LogInfo("GMT_Reset: recording file reopened.");
      break;

    case GMT_Mode_REPLAY:
      GMT_Record_FreeReplay();
      GMT_Record_LoadReplay();
      GMT_LogInfo("GMT_Reset: replay data reloaded.");
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
  GMT_LogError("GMT_Fail: test marked as failed on frame %u.", g_gmt.frame_index);
  GMT_Platform_MutexUnlock();

  // Dereference pointer-to-function-pointer pattern used for overridable callbacks.
  if (g_gmt.setup.fail_callback && *g_gmt.setup.fail_callback) {
    GMT_FailCallback cb = *g_gmt.setup.fail_callback;
    cb();
  } else {
    GMT_DefaultFail();
  }
}
