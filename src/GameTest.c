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
  GMT_PrintReport();
  // Use abort instead of exit so that a debugger can catch it.
  // In CI the process will terminate with a non-zero code either way.
  abort();
}

// ===== Init / Quit =====

bool GMT_Init(const GMT_Setup* setup) {
  if (!setup) return false;
  if (g_gmt.initialized) {
    GMT_LogWarning("GMT_Init called while already initialized; call GMT_Quit first.");
    return false;
  }

  // Zero the state before populating it.
  memset(&g_gmt, 0, sizeof(g_gmt));

  // Install platform input hooks (e.g. mouse wheel accumulator).
  GMT_Platform_Init();

  // Shallow-copy the setup.  The caller is responsible for keeping any
  // strings and arrays (test_path, work_dir, directory_mappings) alive.
  g_gmt.setup = *setup;
  g_gmt.mode = setup->mode;

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
      break;

    case GMT_Mode_REPLAY:
      if (!GMT_Record_LoadReplay()) {
        GMT_LogError("GMT_Init: failed to load test file for replay.");
        memset(&g_gmt, 0, sizeof(g_gmt));
        return false;
      }
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

  return true;
}

void GMT_Quit(void) {
  if (!g_gmt.initialized) return;

  // Finalise recording / replay.
  switch (g_gmt.mode) {
    case GMT_Mode_RECORD:
      GMT_Record_CloseWrite();
      break;
    case GMT_Mode_REPLAY:
      GMT_Record_FreeReplay();
      break;
    default:
      break;
  }

  GMT_PrintReport();

  GMT_Platform_Quit();
  memset(&g_gmt, 0, sizeof(g_gmt));
}

// ===== Runtime =====

void GMT_Update(void) {
  if (!g_gmt.initialized) return;

  GMT_Platform_MutexLock();

  switch (g_gmt.mode) {
    case GMT_Mode_RECORD:
      GMT_Record_WriteFrame();
      break;
    case GMT_Mode_REPLAY:
      GMT_Record_InjectFrame();
      break;
    default:
      break;
  }

  g_gmt.frame_index++;

  GMT_Platform_MutexUnlock();
}

void GMT_Reset(void) {
  if (!g_gmt.initialized) return;

  GMT_Platform_MutexLock();

  // Tear down the current recording / replay session.
  switch (g_gmt.mode) {
    case GMT_Mode_RECORD:
      // Close the current file (writes TAG_END) and start a new one.
      GMT_Record_CloseWrite();
      // Reopen; any existing data is overwritten.
      GMT_Record_OpenForWrite();
      break;

    case GMT_Mode_REPLAY:
      GMT_Record_FreeReplay();
      GMT_Record_LoadReplay();
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

void GMT_Fail(void) {
  if (!g_gmt.initialized) return;

  GMT_Platform_MutexLock();
  g_gmt.test_failed = true;
  GMT_Platform_MutexUnlock();

  // Dereference pointer-to-function-pointer pattern used for overridable callbacks.
  if (g_gmt.setup.fail_callback && *g_gmt.setup.fail_callback) {
    GMT_FailCallback cb = *g_gmt.setup.fail_callback;
    cb();
  } else {
    GMT_DefaultFail();
  }
}
