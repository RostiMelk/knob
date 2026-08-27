#pragma once

/// Index into KAFFI_OFFICES stored in NVS, or -1 when never chosen
/// (first boot — show the office picker).
int kaffi_office_get();

/// Persist the office choice. Out-of-range values are ignored.
void kaffi_office_set(int index);
