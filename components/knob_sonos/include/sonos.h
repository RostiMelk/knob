#pragma once

#include <cstdint>

void sonos_init();
void sonos_start();
void sonos_stop();

void sonos_set_speaker(const char *ip, int port = 1400);

// Register station URLs for URI-to-index matching during state polling
void sonos_set_stations(const char *const *urls, int count);

void sonos_play_uri(const char *uri);
void sonos_play();
void sonos_pause();
void sonos_stop_playback();

// Send Stop directly to a specific speaker IP (bypasses net task queue).
// Use this to stop the old speaker before switching to a new one.
void sonos_stop_playback_at(const char *ip, int port = 1400);

// Add a speaker to the current coordinator's group (multi-room).
// Sends SetAVTransportURI with x-rincon:{coordinator_uuid} to the target.
// Also registers the speaker as a group member for volume sync.
void sonos_group_speaker(const char *target_ip, int target_port,
                         const char *coordinator_uuid);

// Ungroup all group members (each becomes standalone) and clear the list.
// Call before switching speakers to break the old group.
void sonos_clear_group();

// Ungroup a single speaker (becomes standalone) and remove from tracked
// members.
void sonos_ungroup_speaker(const char *ip, int port = 1400);

// Register a speaker as a group member for volume sync (no SOAP command sent).
// Use after discovery to sync pre-existing groups.
void sonos_add_group_member(const char *ip, int port = 1400);

// Number of speakers currently grouped with the coordinator.
int sonos_group_count();

// Set volume on coordinator + all group members.
void sonos_set_volume(int level);
void sonos_previous();
void sonos_next();

// Seek to the beginning of the current track (restart).
void sonos_seek_start();

// Download album art JPEG from the given URL into a caller-provided buffer.
// Returns the number of bytes written, or 0 on failure.
int sonos_fetch_art(const char *url, uint8_t *buf, int buf_size);
