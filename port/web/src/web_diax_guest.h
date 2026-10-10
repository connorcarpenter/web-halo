#ifndef WEB_DIAX_GUEST_H
#define WEB_DIAX_GUEST_H
#include "../diax_guest/diax_guest.h"
#include "game/campaign_coop_adapter.h"
#include <pthread.h>

/* Worker-only slot binding. The candidate guest must be distinct from the
 * currently bound guest. Destroy the prior guest only after successful bind.
 * Read adapter.initial_action_len and take/apply that batch through the Diax
 * byte API before dispatch; pending bytes block events and capture in Rust.
 * This exposes no JS entry and assigns no game kind/action vocabulary. */
int web_diax_guest_make_slots(struct halo_diax_guest *guest,
    struct campaign_coop_adapter_slots *slots);

/* Initialize once before use; never copy a live worker. Bind, byte delivery,
 * capture, restore and close all run on the pthread that first binds it.
 * These functions own the real guest; the host still owns admission metadata,
 * decoding/validating/applying actions and coherent engine checkpoints.
 * In particular, copying action bytes is not proof their effects committed. */
struct web_diax_worker
{
    struct halo_diax_guest *guest;
    struct campaign_coop_adapter adapter;
    pthread_t owner;
    int owner_set;
    int initial_actions_pending;
};

#define WEB_DIAX_WORKER_INITIALIZER \
    { .adapter = { .bound_generation = CAMPAIGN_COOP_ADAPTER_NO_GENERATION } }

/* Stage a distinct guest. Refusal destroys only the candidate and preserves
 * the active binding. A successful bind replaces/destroys the prior guest.
 * Never replace a binding with undelivered init actions, including an empty
 * init batch: deliver it exactly once before events or guest snapshots. */
int web_diax_worker_bind(struct web_diax_worker *worker,
    const uint8_t *blob, uint32_t blob_len,
    int32_t generation, int32_t mode_kind, int32_t required_kind,
    int32_t required_abi_version, int64_t guest_blob_id,
    const uint8_t *init_params, int32_t init_params_len, uint32_t first_serial,
    uint8_t *error, uint32_t error_capacity);
int web_diax_worker_take_initial_actions(struct web_diax_worker *worker,
    uint8_t *actions, int32_t capacity, int32_t *len_out);
int web_diax_worker_dispatch(struct web_diax_worker *worker,
    int32_t generation, uint32_t serial, const uint8_t *event, int32_t event_len,
    uint8_t *actions, int32_t capacity, int32_t *len_out);
/* Guest images alone are not engine checkpoints. The host must bind the
 * matching world image and clear its transient event queues at restoration. */
int web_diax_worker_capture_guest(struct web_diax_worker *worker,
    uint8_t *image, int32_t capacity, int32_t *len_out);
int web_diax_worker_restore_guest(struct web_diax_worker *worker,
    int32_t generation, uint32_t first_serial, int64_t guest_blob_id,
    const uint8_t *image, int32_t image_len);
/* Idempotent on the owner; a foreign pthread refuses without freeing state. */
int web_diax_worker_close(struct web_diax_worker *worker);
#endif
