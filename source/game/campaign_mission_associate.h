/* Campaign mission encounter association (Vehicle A, Hazard Run
 * play-path slice, Finn).
 *
 * Consumes resolver PENDING_ANCHOR encounters: every anchored authored
 * encounter binds to a real firing position in the loaded scenario.
 * The encounter-table leg is positional: authored encounter N drives
 * scenario ai_encounters row N, so the author orders the record to
 * match the map's encounter table. The firing-position leg is the
 * record's anchor_index, a row of that encounter's firing_positions.
 * Both indices address real scenario data; no format is invented here.
 * The association never spawns encounters and never mutates the
 * scenario; it only reports bindings for the encounter layer.
 *
 * Call after the resolver reports OK or PENDING_ANCHOR; volume range
 * stays the resolver's lane and is not re-checked here. Returns only
 * CAMPAIGN_MISSION_APPLY_OK (every anchored encounter bound) or
 * CAMPAIGN_MISSION_APPLY_REFUSED (both from
 * game/campaign_mission_apply.h, unchanged). campaign_mission_activate
 * keeps its contract: PENDING_ANCHOR still commits nothing; the
 * encounter layer associates the committed record when it lands.
 */

#ifndef CAMPAIGN_MISSION_ASSOCIATE_H
#define CAMPAIGN_MISSION_ASSOCIATE_H

#include <stddef.h>
#include <stdint.h>

#include "game/campaign_mission.h"

/* One bound anchored encounter: the authored encounter position (hence
 * the scenario ai_encounters row, by the positional rule) and the
 * firing_positions row its anchor named. Flat (int16_t only) so a
 * binding table is trivially copyable. */
struct campaign_mission_binding
{
	int16_t encounter;
	int16_t firing_position;
};

/* Scenario layout (struct scenario, ai_encounters of
 * encounter_definition); forward-declared so this header stays
 * includable without tag types. */
struct scenario;

/* Scenario resolver report (defined in game/campaign_mission_apply.h);
 * forward-declared so this header stays includable without tag types. */
struct campaign_mission_apply_report;

/* Binds every anchored encounter of mission against scenario. Every
 * authored position must exist in the table (authored N drives row N
 * whether anchored or not). Unanchored
 * encounters bind nothing (they spawn at the volume default) and consume
 * no capacity. On CAMPAIGN_MISSION_APPLY_OK every anchored encounter
 * has a binding in bindings[0 .. *bound_out). On
 * CAMPAIGN_MISSION_APPLY_REFUSED (bad record, no scenario row, anchor
 * past the encounter's firing_positions, or binding room exhausted)
 * bindings holds only the encounters bound before the refusal and the
 * activation stays uncommitted: a partial table is never presented as
 * complete. bound_out reports the bound-so-far count on every return
 * unless it is NULL; bindings may be NULL only when no anchored
 * encounter needs a row. Neither input is mutated. */
int campaign_mission_associate(
	const struct campaign_mission *mission,
	const struct scenario *scenario,
	struct campaign_mission_binding *bindings,
	int16_t binding_capacity,
	int16_t *bound_out);

/* Encounter-layer bind point. Loads the episode record, resolves it
 * against the loaded scenario's trigger_volumes, associates every
 * anchored encounter, and commits the record with its binding table
 * only on the fully bound outcome. Returns CAMPAIGN_MISSION_APPLY_OK
 * (bound_out holds the table length, possibly zero for anchor-free
 * records) or CAMPAIGN_MISSION_APPLY_REFUSED; the resolver's
 * PENDING_ANCHOR never escapes: it becomes OK when every anchor binds
 * and REFUSED otherwise, with any prior activation untouched either
 * way. campaign_mission_activate keeps its own contract untouched.
 * Implemented in game/campaign_mission.c beside the commit it shares;
 * the pure bind stays above. The future JS readback names
 * platform_web_campaign_mission_activated_episode (not yet exported). */
int campaign_mission_activate_bound(
	int16_t episode_index,
	struct campaign_mission_apply_report *report,
	int16_t *bound_out);

/* Last bound table, or NULL before the first bound activation. Read by
 * the encounter layer; never mutated in place. The table holds
 * *count_out rows when count_out is not NULL. */
const struct campaign_mission_binding *campaign_mission_bound_table(
	int16_t *count_out);

#endif /* CAMPAIGN_MISSION_ASSOCIATE_H */
