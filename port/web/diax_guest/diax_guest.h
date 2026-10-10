#ifndef HALO_DIAX_GUEST_H
#define HALO_DIAX_GUEST_H
#include <stdint.h>

/* Opaque existing-Diax guest, owned by exactly one simulation pthread.
 * No entry is a browser-main-thread/JS API. Call and destroy only on the owner.
 * Buffers must remain valid and disjoint from the handle throughout the call;
 * NULL is allowed only with zero length. All inputs/outputs are copied bytes.
 * Negative byte counts refuse; inspect halo_diax_error for runtime detail. */
struct halo_diax_guest;
struct halo_diax_guest *halo_diax_create(const uint8_t *blob, uint32_t len,
    uint8_t *error, uint32_t error_capacity);
void halo_diax_destroy(struct halo_diax_guest *guest);
uint32_t halo_diax_abi_version(const struct halo_diax_guest *guest);
/* init=1 exactly once; init=0 for events. Returns pending action byte count.
 * Includes __init action bytes. Must take output before another call/capture.
 * A runtime trap faults this instance until a successful saved-image restore. */
int32_t halo_diax_call(struct halo_diax_guest *guest, int32_t init,
    const uint8_t *input, uint32_t len);
/* Short buffers refuse and retain the entire pending batch; never call again
 * to retrieve output. The caller decodes/applies the game's action vocabulary. */
int32_t halo_diax_take_output(struct halo_diax_guest *guest,
    uint8_t *output, uint32_t capacity);
/* Adapter call+copy. An oversized batch faults the guest until checkpoint
 * rollback; never retry a guest event whose output did not fit. */
int32_t halo_diax_event(struct halo_diax_guest *guest,
    const uint8_t *input, uint32_t len, uint8_t *actions, uint32_t capacity);
int32_t halo_diax_capture(struct halo_diax_guest *guest,
    uint8_t *image, uint32_t capacity);
/* Fresh instance from stored SAME blob, memory restored without __init.
 * Invalid images leave the current guest intact. Whole Wasm pages, not records. */
int32_t halo_diax_restore(struct halo_diax_guest *guest,
    const uint8_t *image, uint32_t len);
/* Copies the last refusal as NUL-terminated UTF-8; capacity must include NUL. */
int32_t halo_diax_error(const struct halo_diax_guest *guest,
    uint8_t *error, uint32_t capacity);
#endif
