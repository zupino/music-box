// SPDX-License-Identifier: MIT
#include "rt_queue.h"

#define QUEUE_MASK (MUSIC_BOX_QUEUE_CAPACITY - 1u)

#if (MUSIC_BOX_QUEUE_CAPACITY & (MUSIC_BOX_QUEUE_CAPACITY - 1u)) != 0
#error "MUSIC_BOX_QUEUE_CAPACITY must be a power of two"
#endif

void music_box_command_queue_init(MusicBoxCommandQueue *queue)
{
    atomic_init(&queue->read_index, 0u);
    atomic_init(&queue->write_index, 0u);
}

bool music_box_command_queue_push(MusicBoxCommandQueue *queue,
                                  const MusicBoxCommand *command)
{
    const unsigned int write =
        atomic_load_explicit(&queue->write_index, memory_order_relaxed);
    const unsigned int next = (write + 1u) & QUEUE_MASK;
    const unsigned int read =
        atomic_load_explicit(&queue->read_index, memory_order_acquire);

    if (next == read) {
        return false;
    }

    queue->items[write] = *command;
    atomic_store_explicit(&queue->write_index, next, memory_order_release);
    return true;
}

bool music_box_command_queue_pop(MusicBoxCommandQueue *queue,
                                 MusicBoxCommand *command)
{
    const unsigned int read =
        atomic_load_explicit(&queue->read_index, memory_order_relaxed);
    const unsigned int write =
        atomic_load_explicit(&queue->write_index, memory_order_acquire);

    if (read == write) {
        return false;
    }

    *command = queue->items[read];
    atomic_store_explicit(&queue->read_index,
                          (read + 1u) & QUEUE_MASK,
                          memory_order_release);
    return true;
}

void music_box_event_queue_init(MusicBoxEventQueue *queue)
{
    atomic_init(&queue->read_index, 0u);
    atomic_init(&queue->write_index, 0u);
}

bool music_box_event_queue_push(MusicBoxEventQueue *queue,
                                const MusicBoxEvent *event)
{
    const unsigned int write =
        atomic_load_explicit(&queue->write_index, memory_order_relaxed);
    const unsigned int next = (write + 1u) & QUEUE_MASK;
    const unsigned int read =
        atomic_load_explicit(&queue->read_index, memory_order_acquire);

    if (next == read) {
        return false;
    }

    queue->items[write] = *event;
    atomic_store_explicit(&queue->write_index, next, memory_order_release);
    return true;
}

bool music_box_event_queue_pop(MusicBoxEventQueue *queue,
                               MusicBoxEvent *event)
{
    const unsigned int read =
        atomic_load_explicit(&queue->read_index, memory_order_relaxed);
    const unsigned int write =
        atomic_load_explicit(&queue->write_index, memory_order_acquire);

    if (read == write) {
        return false;
    }

    *event = queue->items[read];
    atomic_store_explicit(&queue->read_index,
                          (read + 1u) & QUEUE_MASK,
                          memory_order_release);
    return true;
}
