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

static int guest_capture(void *context, uint8_t *output, int32_t capacity)
{
    if (capacity < 0) return -1;
    return halo_diax_capture(context, output, (uint32_t)capacity);
}

static int guest_restore(void *context, const uint8_t *input, int32_t len)
{
    if (len < 0) return -1;
    return halo_diax_restore(context, input, (uint32_t)len);
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
