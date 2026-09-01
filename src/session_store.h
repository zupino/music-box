// SPDX-License-Identifier: MIT
#ifndef MUSIC_BOX_SESSION_STORE_H
#define MUSIC_BOX_SESSION_STORE_H

#include "config.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool enabled;
    uint32_t sample_rate;
    char directory[MUSIC_BOX_NAME_LENGTH];
} MusicBoxSessionStore;

bool music_box_session_store_init(MusicBoxSessionStore *store,
                                  const MusicBoxConfig *config,
                                  uint32_t sample_rate);
bool music_box_session_store_load(const MusicBoxSessionStore *store,
                                  MusicBoxLoopBank *samples,
                                  MusicBoxLoopBank *loops,
                                  MusicBoxEventQueue *events);
bool music_box_session_store_save_sample(const MusicBoxSessionStore *store,
                                         const MusicBoxLoopBank *samples,
                                         uint8_t slot);
bool music_box_session_store_save_manifest(const MusicBoxSessionStore *store,
                                           const MusicBoxLoopBank *samples,
                                           const MusicBoxLoopBank *loops);

#endif
