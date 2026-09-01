// SPDX-License-Identifier: MIT
#include "session_store.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    char magic[8];
    uint32_t sample_rate;
    uint32_t reserved;
    uint64_t frames;
} SampleHeader;

static bool write_all(int fd, const void *buffer, size_t bytes)
{
    const unsigned char *cursor = buffer;
    while (bytes > 0u) {
        const ssize_t written = write(fd, cursor, bytes);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) return false;
        cursor += (size_t)written;
        bytes -= (size_t)written;
    }
    return true;
}

static bool read_all(int fd, void *buffer, size_t bytes)
{
    unsigned char *cursor = buffer;
    while (bytes > 0u) {
        const ssize_t count = read(fd, cursor, bytes);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return false;
        cursor += (size_t)count;
        bytes -= (size_t)count;
    }
    return true;
}

static bool path_for(char *path, size_t capacity, const MusicBoxSessionStore *store,
                     const char *name, uint8_t slot, bool temporary)
{
    const int count = slot < MUSIC_BOX_LOOP_COUNT
        ? snprintf(path, capacity, "%s/%s-%02u.bin%s", store->directory, name,
                   (unsigned int)slot + 1u, temporary ? ".tmp" : "")
        : snprintf(path, capacity, "%s/%s%s", store->directory, name,
                   temporary ? ".tmp" : "");
    return count > 0 && (size_t)count < capacity;
}

static bool sync_directory(const MusicBoxSessionStore *store)
{
    const int fd = open(store->directory, O_RDONLY | O_DIRECTORY);
    if (fd < 0) return false;
    const bool ok = fsync(fd) == 0;
    close(fd);
    return ok;
}

bool music_box_session_store_init(MusicBoxSessionStore *store,
                                  const MusicBoxConfig *config,
                                  uint32_t sample_rate)
{
    memset(store, 0, sizeof(*store));
    store->enabled = config->session_persistence_enabled;
    store->sample_rate = sample_rate;
    memcpy(store->directory, config->session_directory,
           sizeof(store->directory));
    if (!store->enabled) return true;
    if (mkdir(store->directory, 0755) != 0 && errno != EEXIST) return false;
    struct stat status;
    return stat(store->directory, &status) == 0 && S_ISDIR(status.st_mode);
}

bool music_box_session_store_save_sample(const MusicBoxSessionStore *store,
                                         const MusicBoxLoopBank *samples,
                                         uint8_t slot_index)
{
    if (!store->enabled) return true;
    if (slot_index >= MUSIC_BOX_LOOP_COUNT) return false;
    const MusicBoxLoopSlot *slot = &samples->slots[slot_index];
    if (slot->recorded_frames == 0u) return false;
    char temporary[160];
    char final[160];
    if (!path_for(temporary, sizeof(temporary), store, "sample", slot_index, true) ||
        !path_for(final, sizeof(final), store, "sample", slot_index, false))
        return false;
    const int fd = open(temporary, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return false;
    const SampleHeader header = {
        .magic = {'M', 'B', 'S', 'A', 'M', 'P', '1', '\0'},
        .sample_rate = store->sample_rate,
        .reserved = 0u,
        .frames = slot->recorded_frames,
    };
    bool ok = write_all(fd, &header, sizeof(header)) &&
        write_all(fd, slot->left, slot->recorded_frames * sizeof(float)) &&
        write_all(fd, slot->right, slot->recorded_frames * sizeof(float)) &&
        fdatasync(fd) == 0;
    if (close(fd) != 0) ok = false;
    if (ok) ok = rename(temporary, final) == 0 && sync_directory(store);
    if (!ok) (void)unlink(temporary);
    return ok;
}

bool music_box_session_store_save_manifest(const MusicBoxSessionStore *store,
                                           const MusicBoxLoopBank *samples,
                                           const MusicBoxLoopBank *loops)
{
    if (!store->enabled) return true;
    char temporary[160];
    char final[160];
    if (!path_for(temporary, sizeof(temporary), store, "manifest", UINT8_MAX, true) ||
        !path_for(final, sizeof(final), store, "manifest", UINT8_MAX, false))
        return false;
    FILE *file = fopen(temporary, "w");
    if (file == NULL) return false;
    bool ok = fprintf(file, "version 2\nrate %u\n", store->sample_rate) > 0;
    for (uint8_t index = 0u; ok && index < MUSIC_BOX_LOOP_COUNT; ++index) {
        const MusicBoxLoopSlot *slot = &samples->slots[index];
        ok = fprintf(file, "sample %u %zu %zu %zu\n", (unsigned int)index,
                     slot->recorded_frames, slot->trim_start, slot->trim_end) > 0;
    }
    for (uint8_t index = 0u; ok && index < MUSIC_BOX_LOOP_COUNT; ++index)
        ok = fprintf(file, "loop %u %u\n", (unsigned int)index,
                     (unsigned int)loops->slots[index].assigned_sample) > 0;
    if (ok) ok = fflush(file) == 0 && fsync(fileno(file)) == 0;
    if (fclose(file) != 0) ok = false;
    if (ok) ok = rename(temporary, final) == 0 && sync_directory(store);
    if (!ok) (void)unlink(temporary);
    return ok;
}

static bool load_sample(const MusicBoxSessionStore *store, MusicBoxLoopBank *samples,
                        uint8_t slot_index)
{
    char path[160];
    if (!path_for(path, sizeof(path), store, "sample", slot_index, false)) return false;
    const int fd = open(path, O_RDONLY);
    if (fd < 0) return errno == ENOENT;
    SampleHeader header;
    MusicBoxLoopSlot *slot = &samples->slots[slot_index];
    bool ok = read_all(fd, &header, sizeof(header)) &&
        memcmp(header.magic, "MBSAMP1", 7u) == 0 &&
        header.sample_rate == store->sample_rate &&
        header.frames > 0u && header.frames <= slot->capacity_frames;
    if (ok) {
        const size_t frames = (size_t)header.frames;
        ok = read_all(fd, slot->left, frames * sizeof(float)) &&
             read_all(fd, slot->right, frames * sizeof(float));
        if (ok) {
            slot->recorded_frames = frames;
            slot->trim_start = 0u;
            slot->trim_end = frames;
            slot->play_position = 0u;
            slot->playback_left = slot->left;
            slot->playback_right = slot->right;
            slot->state = MUSIC_BOX_LOOP_STOPPED;
        }
    }
    close(fd);
    return ok;
}

bool music_box_session_store_load(const MusicBoxSessionStore *store,
                                  MusicBoxLoopBank *samples,
                                  MusicBoxLoopBank *loops,
                                  MusicBoxEventQueue *events)
{
    if (!store->enabled) return true;
    bool ok = true;
    for (uint8_t index = 0u; index < MUSIC_BOX_LOOP_COUNT; ++index) {
        if (!load_sample(store, samples, index)) ok = false;
        if (samples->slots[index].recorded_frames > 0u && events != NULL)
            (void)music_box_event_queue_push(events, &(MusicBoxEvent){
                .type = MUSIC_BOX_EVENT_RESTORED_SAMPLE, .slot = index,
                .bank = (uint8_t)MUSIC_BOX_MODE_SAMPLER,
                .value = samples->slots[index].recorded_frames});
    }

    char path[160];
    if (!path_for(path, sizeof(path), store, "manifest", UINT8_MAX, false)) return false;
    FILE *file = fopen(path, "r");
    if (file == NULL) return errno == ENOENT ? ok : false;
    unsigned int version = 0u;
    char line[160];
    while (fgets(line, sizeof(line), file) != NULL) {
        unsigned int index = 0u;
        unsigned long long frames = 0u, start = 0u, end = 0u;
        if (sscanf(line, "version %u", &version) == 1) {
            continue;
        }
        if (sscanf(line, "sample %u %llu %llu %llu",
                   &index, &frames, &start, &end) == 4) {
            if (index < MUSIC_BOX_LOOP_COUNT &&
                samples->slots[index].recorded_frames == (size_t)frames &&
                start < end && end <= frames) {
                samples->slots[index].trim_start = (size_t)start;
                samples->slots[index].trim_end = (size_t)end;
            }
        } else {
            unsigned int sample = UINT8_MAX;
            if (sscanf(line, "loop %u %u", &index, &sample) != 2)
                continue;
            /* Version 1 accidentally serialized every unassigned loop as S1
               because zero-initialized assigned_sample was not UINT8_MAX.
               Ignore ambiguous S1 entries from that format; genuine S1
               assignments can be made again and are unambiguous in v2. */
            if (version < 2u && sample == 0u) continue;
            if (index < MUSIC_BOX_LOOP_COUNT && sample < MUSIC_BOX_LOOP_COUNT) {
                MusicBoxEventQueue *saved_events = loops->events;
                loops->events = NULL;
                const bool assigned = music_box_loop_bank_assign(
                    loops, (uint8_t)index, samples, (uint8_t)sample);
                loops->events = saved_events;
                if (assigned && events != NULL)
                    (void)music_box_event_queue_push(events, &(MusicBoxEvent){
                        .type = MUSIC_BOX_EVENT_RESTORED_LOOP,
                        .slot = (uint8_t)index,
                        .bank = (uint8_t)MUSIC_BOX_MODE_LOOP,
                        .value = sample});
            }
        }
    }
    if (ferror(file)) ok = false;
    fclose(file);
    return ok;
}
