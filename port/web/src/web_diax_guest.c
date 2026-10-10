#include "web_diax_guest.h"

static int32_t guest_abi(void *context)
{
    return (int32_t)halo_diax_abi_version(context);
}

static int guest_init(void *context, const uint8_t *input, int32_t len)
{
    if (len < 0) return -1;
    return halo_diax_call(context, 1, input, (uint32_t)len);
}

static int guest_event(void *context, const uint8_t *input, int32_t len,
    uint8_t *output, int32_t capacity)
{
    if (len < 0 || capacity < 0) return -1;
    return halo_diax_event(context, input, (uint32_t)len,
        output, (uint32_t)capacity);
}

static int guest_capture(void *context, uint8_t *output, int32_t capacity,
    int32_t generation, uint32_t serial)
{
    if (capacity < 0) return -1;
    return halo_diax_capture(context, output, (uint32_t)capacity, generation, serial);
}

static int guest_restore(void *context, const uint8_t *input, int32_t len,
    int32_t generation, uint32_t serial)
{
    if (len < 0) return -1;
    return halo_diax_restore(context, input, (uint32_t)len, generation, serial);
}

int web_diax_guest_make_slots(struct halo_diax_guest *guest,
    struct campaign_coop_adapter_slots *slots)
{
    if (!guest || !slots) return -1;
    slots->guest_context = guest;
    slots->guest_abi_version = guest_abi;
    slots->guest_init = guest_init;
    slots->guest_handle_event = guest_event;
    slots->guest_capture_image = guest_capture;
    slots->guest_restore_image = guest_restore;
    return 0;
}

static int worker_on_owner(const struct web_diax_worker *worker)
{
    return worker && (!worker->owner_set || pthread_equal(worker->owner, pthread_self()));
}

static int worker_ready(const struct web_diax_worker *worker)
{
    return worker_on_owner(worker) && worker->guest &&
        !worker->initial_actions_pending && campaign_coop_adapter_bound(&worker->adapter);
}

int web_diax_worker_bind(struct web_diax_worker *worker,
    const uint8_t *blob, uint32_t blob_len,
    int32_t generation, int32_t mode_kind, int32_t required_kind,
    int32_t required_abi_version, int64_t guest_blob_id,
    const uint8_t *init_params, int32_t init_params_len, uint32_t first_serial,
    uint8_t *error, uint32_t error_capacity)
{
    struct halo_diax_guest *candidate;
    struct halo_diax_guest *prior;
    struct campaign_coop_adapter staged = {
        .bound_generation = CAMPAIGN_COOP_ADAPTER_NO_GENERATION
    };
    struct campaign_coop_adapter_slots slots;

    if (!worker_on_owner(worker) || worker->initial_actions_pending ||
        !blob || !blob_len || generation < 0 || mode_kind != required_kind ||
        init_params_len < 0 || init_params_len > CAMPAIGN_COOP_ADAPTER_MAX_PAYLOAD ||
        (!init_params && init_params_len))
        return CAMPAIGN_COOP_ADAPTER_REFUSED;
    /* Replacement is also a generation boundary: queued traffic from the
     * prior guest must never become valid against the new guest's serial. */
    if (worker->guest && generation <= worker->adapter.bound_generation)
        return CAMPAIGN_COOP_ADAPTER_REFUSED;

    candidate = halo_diax_create(blob, blob_len, error, error_capacity);
    if (!candidate) return CAMPAIGN_COOP_ADAPTER_REFUSED;
    if (web_diax_guest_make_slots(candidate, &slots) != 0 ||
        campaign_coop_adapter_bind(&staged, generation, mode_kind, required_kind,
            required_abi_version, guest_blob_id, &slots, init_params,
            init_params_len, first_serial) != CAMPAIGN_COOP_ADAPTER_OK)
    {
        halo_diax_error(candidate, error, error_capacity);
        halo_diax_destroy(candidate);
        return CAMPAIGN_COOP_ADAPTER_REFUSED;
    }

    prior = worker->guest;
    worker->guest = candidate;
    worker->adapter = staged;
    worker->owner = pthread_self();
    worker->owner_set = 1;
    worker->initial_actions_pending = 1;
    halo_diax_destroy(prior);
    return CAMPAIGN_COOP_ADAPTER_OK;
}

int web_diax_worker_take_initial_actions(struct web_diax_worker *worker,
    uint8_t *actions, int32_t capacity, int32_t *len_out)
{
    int32_t produced;
    if (!worker_on_owner(worker) || !worker->guest ||
        !worker->initial_actions_pending || capacity < 0 || !len_out)
        return CAMPAIGN_COOP_ADAPTER_REFUSED;
    produced = halo_diax_take_output(worker->guest, actions, (uint32_t)capacity);
    if (produced < 0) return CAMPAIGN_COOP_ADAPTER_REFUSED;
    *len_out = produced;
    worker->initial_actions_pending = 0;
    worker->adapter.initial_action_len = 0;
    return CAMPAIGN_COOP_ADAPTER_OK;
}

int web_diax_worker_dispatch(struct web_diax_worker *worker,
    int32_t generation, uint32_t serial, const uint8_t *event, int32_t event_len,
    uint8_t *actions, int32_t capacity, int32_t *len_out)
{
    if (!worker_ready(worker)) return CAMPAIGN_COOP_ADAPTER_REFUSED;
    return campaign_coop_adapter_dispatch(&worker->adapter, generation, serial,
        event, event_len, actions, capacity, len_out);
}

int web_diax_worker_capture_guest(struct web_diax_worker *worker,
    uint8_t *image, int32_t capacity, int32_t *len_out)
{
    if (!worker_ready(worker)) return CAMPAIGN_COOP_ADAPTER_REFUSED;
    return campaign_coop_adapter_capture(&worker->adapter, image, capacity, len_out);
}

int web_diax_worker_restore_guest(struct web_diax_worker *worker,
    int32_t generation, uint32_t first_serial, int64_t guest_blob_id,
    const uint8_t *image, int32_t image_len)
{
    if (!worker_ready(worker)) return CAMPAIGN_COOP_ADAPTER_REFUSED;
    return campaign_coop_adapter_restore(&worker->adapter, generation,
        first_serial, guest_blob_id, image, image_len);
}

int web_diax_worker_close(struct web_diax_worker *worker)
{
    struct halo_diax_guest *prior;
    struct web_diax_worker empty = WEB_DIAX_WORKER_INITIALIZER;
    if (!worker_on_owner(worker)) return CAMPAIGN_COOP_ADAPTER_REFUSED;
    prior = worker->guest;
    /* Closing a guest does not release this worker's pthread ownership. */
    empty.owner = worker->owner;
    empty.owner_set = worker->owner_set;
    *worker = empty;
    halo_diax_destroy(prior);
    return CAMPAIGN_COOP_ADAPTER_OK;
}
