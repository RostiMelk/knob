#pragma once

/// Confirm the running firmware after a successful people fetch. With
/// bootloader rollback enabled, an OTA image that never calls this gets
/// rolled back on the next reset — the safety net for remote updates.
void kaffi_ota_mark_valid();

/// Compare the published firmwareRelease against the running version and,
/// when newer, download + apply it on a background task (reboots on
/// success). No-op while a check is already running.
void kaffi_ota_check();
