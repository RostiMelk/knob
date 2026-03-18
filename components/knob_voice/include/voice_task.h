#pragma once

// ─── Voice Task ─────────────────────────────────────────────────────────────
//
// Main orchestrator for voice mode. Manages the lifecycle:
//   1. Connect WebSocket to OpenAI Realtime API
//   2. Send session.update with tools + config
//   3. Start mic capture, stream audio as input_audio_buffer.append
//   4. Handle incoming events (audio deltas, transcripts, tool calls)
//   5. Clean up on deactivation or error
//
// Runs as a FreeRTOS task on Core 1. Communicates with UI via APP_EVENTs.

// Initialize voice task infrastructure (call once from app_main).
// Does NOT start the task — waits for voice_task_start().
void voice_task_init();

// Start voice mode: creates the FreeRTOS task, connects WebSocket.
// Called when APP_EVENT_VOICE_ACTIVATE fires.
void voice_task_start();

// Stop voice mode: tears down WebSocket, stops mic/audio, deletes task.
// Called when APP_EVENT_VOICE_DEACTIVATE fires or on error/timeout.
void voice_task_stop();

// Check if voice task is currently running.
bool voice_task_is_active();
