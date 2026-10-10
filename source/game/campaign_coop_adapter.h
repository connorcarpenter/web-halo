/* Hazard Run cooperative guest adapter (Vehicle A, adapter slice, Finn).
 *
 * Serialized host-adapter/event/action/restore call path for the single
 * sim-worker-owned guest instance. Reviewed architecture (GDD
 * TECHNICAL/HAZARD_RUN_ARCHITECTURE.md §2, §6): one Diax Wasm mode
 * system, unshared guest memory, copied byte payloads, generation-bound
 * restore that clears pre-restore transient traffic. Brock 61850 restore
 * rule: fresh same-blob guest state, no post-restore __init, coherent
 * engine/guest generation.
 *
 * Parameter-first schema binding: every schema-owned value (cooperative
 * kind marker, required ABI version, guest blob identity, event/action
 * payload meaning) is injected by the caller at bind. The adapter holds
 * no kind vocabulary, no ABI constant, and no guessed entry names; the
 * guest slot table carries the five spec-named entries (__abi_version,
 * __describe_schema is read by the wrapper at publish time, __init,
 * __on_event, snapshot capture/restore over the memory-image boundary).
 * An unbound adapter, a kind mismatch, or a missing slot refuses every
 * call; PENDING never escapes because there is no half-bound state.
 *
 * The core is static inline so the exact bytes run in the engine and in
 * the standalone native controls test. All state lives in the
 * caller-supplied struct; the adapter reads no engine globals, so the
 * sim worker's serialized ownership is structural, not asserted.
 */

#ifndef CAMPAIGN_COOP_ADAPTER_H
#define CAMPAIGN_COOP_ADAPTER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum
{
	/* Preflight refusals do not call the guest. Runtime errors retain
	 * binding/fence metadata but may fault mutated guest state; only a
	 * coherent saved-image restore permits further execution then. */
	CAMPAIGN_COOP_ADAPTER_REFUSED = -1,

	/* The call completed against the bound generation. */
	CAMPAIGN_COOP_ADAPTER_OK = 0,

	/* No generation bound: every dispatch/capture/restore refuses. */
	CAMPAIGN_COOP_ADAPTER_NO_GENERATION = -1,

	/* Adapter-local ceiling for init/event records. Complete guest
	 * memory images use the runtime's memory budget, not this record
	 * limit: a Wasm memory page is already larger than this ceiling.
	 * The snapshot slot validates the complete image before restore. */
	CAMPAIGN_COOP_ADAPTER_MAX_PAYLOAD = 4096,
};

/* Guest entries the runtime wrapper plugs in. Payloads are opaque bytes:
 * the guest (bound to the canonical schema at publish time) interprets
 * them; the adapter only carries, fences, and orders. Negative guest
 * returns are guest refusals and surface as REFUSED. */
struct campaign_coop_adapter_slots
{
	/* Opaque worker-owned guest; each binding retains its own instance.
	 * Caller preserves this context until the binding is replaced/destroyed. */
	void *guest_context;
	/* Spec __abi_version: the guest's stamped ABI version. */
	int32_t (*guest_abi_version)(void *guest_context);
	/* Spec __init: runs exactly once on bind; returns pending action length.
	 * Host must consume/apply these actions before dispatch or checkpoint.
	 * Negative is refusal. The runtime retains the bytes until consumed. */
	int (*guest_init)(void *guest_context, const uint8_t *params_in, int32_t params_len);
	/* Spec __on_event: one serialized mission event in, one action
	 * out. Returns the action byte length, or negative on refusal. */
	int (*guest_handle_event)(void *guest_context, const uint8_t *event_in, int32_t event_len,
		uint8_t *action_out, int32_t action_capacity);
	/* Memory-image capture: returns the image byte length, or
	 * negative on refusal. */
	int (*guest_capture_image)(void *guest_context, uint8_t *image_out, int32_t image_capacity,
		int32_t generation, uint32_t next_serial);
	/* Memory-image restore of a fresh same-blob image: 0 on success,
	 * nonzero otherwise. Never re-runs __init (structural: the
	 * restore path below holds no init call). */
	int (*guest_restore_image)(void *guest_context, const uint8_t *image_in, int32_t image_len,
		int32_t expected_generation, uint32_t expected_next_serial);
};

struct campaign_coop_adapter
{
	/* Bound live generation, or CAMPAIGN_COOP_ADAPTER_NO_GENERATION. */
	int32_t bound_generation;
	/* Schema-assigned cooperative kind, injected at bind. */
	int32_t bound_kind;
	/* Required guest ABI version, injected at bind. */
	int32_t required_abi_version;
	/* Same-blob guest identity, injected at bind. */
	int64_t guest_blob_id;
	/* Gapless worker serial fence: dispatch consumes exactly this
	 * serial, so a stale or replayed event refuses. */
	uint32_t next_serial;
	/* Metadata of the selected captured image. Restores cannot choose an
	 * arbitrary serial or replay an older capture after a newer selection. */
	int checkpoint_selected;
	int32_t checkpoint_generation;
	uint32_t checkpoint_serial;
	/* Bytes from __init that the host must drain/apply; never discard them. */
	int32_t initial_action_len;
	struct campaign_coop_adapter_slots slots;
};

/* True only when every guest slot is plugged. */
static inline bool campaign_coop_adapter_slots_complete(
	const struct campaign_coop_adapter_slots *slots)
{
	return slots && slots->guest_context &&
		slots->guest_abi_version &&
		slots->guest_init &&
		slots->guest_handle_event &&
		slots->guest_capture_image &&
		slots->guest_restore_image;
}

/* True when a generation is bound. */
static inline bool campaign_coop_adapter_bound(
	const struct campaign_coop_adapter *adapter)
{
	return adapter &&
		adapter->bound_generation != CAMPAIGN_COOP_ADAPTER_NO_GENERATION;
}

/* Binds one generation: kind-gate, ABI handshake, then exactly one
 * __init. Both-or-neither: any refusal leaves a prior binding intact
 * and a fresh adapter unbound. mode_kind must equal required_kind (the
 * kind gate is inert until the schema owner assigns the cooperative
 * value the wrapper injects here). */
static inline int campaign_coop_adapter_bind(
	struct campaign_coop_adapter *adapter,
	int32_t generation,
	int32_t mode_kind,
	int32_t required_kind,
	int32_t required_abi_version,
	int64_t guest_blob_id,
	const struct campaign_coop_adapter_slots *slots,
	const uint8_t *init_params,
	int32_t init_params_len,
	uint32_t first_serial)
{
	struct campaign_coop_adapter staged;
	int produced;

	if (!adapter || !campaign_coop_adapter_slots_complete(slots))
		return CAMPAIGN_COOP_ADAPTER_REFUSED;
	if (generation < 0 || mode_kind != required_kind)
		return CAMPAIGN_COOP_ADAPTER_REFUSED;
	if (init_params_len < 0 ||
		init_params_len > CAMPAIGN_COOP_ADAPTER_MAX_PAYLOAD)
		return CAMPAIGN_COOP_ADAPTER_REFUSED;
	if (!init_params && init_params_len > 0)
		return CAMPAIGN_COOP_ADAPTER_REFUSED;
	if (slots->guest_abi_version(slots->guest_context) != required_abi_version)
		return CAMPAIGN_COOP_ADAPTER_REFUSED;
	produced = slots->guest_init(slots->guest_context, init_params, init_params_len);
	if (produced < 0 || produced > CAMPAIGN_COOP_ADAPTER_MAX_PAYLOAD)
		return CAMPAIGN_COOP_ADAPTER_REFUSED;

	staged.bound_generation = generation;
	staged.bound_kind = required_kind;
	staged.required_abi_version = required_abi_version;
	staged.guest_blob_id = guest_blob_id;
	staged.next_serial = first_serial;
	staged.checkpoint_selected = 0;
	staged.checkpoint_generation = CAMPAIGN_COOP_ADAPTER_NO_GENERATION;
	staged.checkpoint_serial = 0;
	staged.initial_action_len = produced;
	staged.slots = *slots;
	*adapter = staged;
	return CAMPAIGN_COOP_ADAPTER_OK;
}

/* One serialized event against the bound generation. Consumes exactly
 * the fenced serial; stale, future, or cross-generation events refuse
 * without touching the guest, so pre-restore traffic can never leak
 * across a restore. A guest refusal preserves the fence. Runtime traps may
 * mutate guest state and fault the instance; a preserved fence alone does
 * not permit retry. The real wrapper refuses until valid image restoration. */
static inline int campaign_coop_adapter_dispatch(
	struct campaign_coop_adapter *adapter,
	int32_t generation,
	uint32_t serial,
	const uint8_t *event_in,
	int32_t event_len,
	uint8_t *action_out,
	int32_t action_capacity,
	int32_t *action_len_out)
{
	int produced;

	if (!campaign_coop_adapter_bound(adapter))
		return CAMPAIGN_COOP_ADAPTER_REFUSED;
	if (generation != adapter->bound_generation || serial != adapter->next_serial)
		return CAMPAIGN_COOP_ADAPTER_REFUSED;
	/* Exhaustion requires an explicit new binding/generation, never wrap. */
	if (serial == UINT32_MAX)
		return CAMPAIGN_COOP_ADAPTER_REFUSED;
	if (!event_in || event_len < 0 ||
		event_len > CAMPAIGN_COOP_ADAPTER_MAX_PAYLOAD)
		return CAMPAIGN_COOP_ADAPTER_REFUSED;
	if (!action_out || action_capacity <= 0 || !action_len_out)
		return CAMPAIGN_COOP_ADAPTER_REFUSED;
	if (action_capacity > CAMPAIGN_COOP_ADAPTER_MAX_PAYLOAD)
		action_capacity = CAMPAIGN_COOP_ADAPTER_MAX_PAYLOAD;

	produced = adapter->slots.guest_handle_event(adapter->slots.guest_context, event_in, event_len,
		action_out, action_capacity);
	if (produced < 0 || produced > action_capacity)
		return CAMPAIGN_COOP_ADAPTER_REFUSED;

	*action_len_out = (int32_t)produced;
	adapter->next_serial++;
	return CAMPAIGN_COOP_ADAPTER_OK;
}

/* Captures the bound guest's memory image into the caller's buffer. */
static inline int campaign_coop_adapter_capture(
	struct campaign_coop_adapter *adapter,
	uint8_t *image_out,
	int32_t image_capacity,
	int32_t *image_len_out)
{
	int produced;

	if (!campaign_coop_adapter_bound(adapter))
		return CAMPAIGN_COOP_ADAPTER_REFUSED;
	if (!image_out || image_capacity <= 0 || !image_len_out)
		return CAMPAIGN_COOP_ADAPTER_REFUSED;

	produced = adapter->slots.guest_capture_image(adapter->slots.guest_context, image_out,
		image_capacity, adapter->bound_generation, adapter->next_serial);
	if (produced < 0 || produced > image_capacity)
		return CAMPAIGN_COOP_ADAPTER_REFUSED;

	*image_len_out = (int32_t)produced;
	adapter->checkpoint_selected = 1;
	adapter->checkpoint_generation = adapter->bound_generation;
	adapter->checkpoint_serial = adapter->next_serial;
	return CAMPAIGN_COOP_ADAPTER_OK;
}

/* Restores the selected same-blob envelope and advances to a new live
 * generation with the captured serial fence. The generation must advance
 * (a same-generation restore refuses); the blob identity must match
 * the bound guest (a foreign blob refuses); a failed restore keeps the
 * prior binding intact. Post-restore dispatch continues without any
 * __init: this path holds no init call by construction. */
static inline int campaign_coop_adapter_restore(
	struct campaign_coop_adapter *adapter,
	int32_t generation,
	uint32_t first_serial,
	int64_t guest_blob_id,
	const uint8_t *image_in,
	int32_t image_len)
{
	if (!campaign_coop_adapter_bound(adapter))
		return CAMPAIGN_COOP_ADAPTER_REFUSED;
	if (generation <= adapter->bound_generation)
		return CAMPAIGN_COOP_ADAPTER_REFUSED;
	if (guest_blob_id != adapter->guest_blob_id)
		return CAMPAIGN_COOP_ADAPTER_REFUSED;
	if (!adapter->checkpoint_selected || first_serial != adapter->checkpoint_serial)
		return CAMPAIGN_COOP_ADAPTER_REFUSED;
	if (!image_in || image_len <= 0)
		return CAMPAIGN_COOP_ADAPTER_REFUSED;
	if (adapter->slots.guest_restore_image(adapter->slots.guest_context, image_in,
		image_len, adapter->checkpoint_generation, adapter->checkpoint_serial) != 0)
		return CAMPAIGN_COOP_ADAPTER_REFUSED;

	adapter->bound_generation = generation;
	adapter->next_serial = first_serial;
	return CAMPAIGN_COOP_ADAPTER_OK;
}

#endif /* CAMPAIGN_COOP_ADAPTER_H */
