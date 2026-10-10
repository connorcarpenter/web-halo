#ifndef WEB_DIAX_GUEST_H
#define WEB_DIAX_GUEST_H
#include "../diax_guest/diax_guest.h"
#include "game/campaign_coop_adapter.h"

/* Worker-only slot binding. The candidate guest must be distinct from the
 * currently bound guest. Destroy the prior guest only after successful bind.
 * Read adapter.initial_action_len and take/apply that batch through the Diax
 * byte API before dispatch; pending bytes block events and capture in Rust.
 * This exposes no JS entry and assigns no game kind/action vocabulary. */
int web_diax_guest_make_slots(struct halo_diax_guest *guest,
    struct campaign_coop_adapter_slots *slots);
#endif
