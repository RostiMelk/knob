#pragma once

/// Start the captive portal WiFi setup, broadcasting an open AP named
/// `ap_name`. Blocks until credentials are submitted and verified, then
/// stops the portal and WiFi — call wifi_manager_init() afterwards.
void wifi_setup_start(const char *ap_name);

/// Returns true if WiFi credentials are stored in NVS (explicitly saved by
/// the user, not the compiled-in Kconfig fallback).
bool wifi_setup_has_credentials();
