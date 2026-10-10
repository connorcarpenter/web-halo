/* Campaign mission scenario resolver (Vehicle A, Hazard Run play-path
 * slice, Roger).
 *
 * Resolves an authored mission record against the loaded scenario's
 * trigger_volumes: every encounter's trigger_volume_index must land
 * inside the map's actual volume range. Anchored encounters
 * (anchor_index != CAMPAIGN_MISSION_NO_ANCHOR) resolve their volume
 * here but report explicitly PENDING until Finn's encounter
 * association consumes them. The resolver never spawns encounters and
 * never mutates the scenario; it is not yet a playable episode.
 *
 * The resolution core is static inline so the exact bytes run in the
 * engine and in the standalone controls test. The report struct is
 * flat (int16_t only, no pointers) so a report is trivially copyable.
 */

#ifndef CAMPAIGN_MISSION_APPLY_H
#define CAMPAIGN_MISSION_APPLY_H

#include <stddef.h>
#include <stdint.h>

#include "game/campaign_mission.h"

enum
{
	/* A volume refused, the record is malformed, or a pointer is
	 * missing. Nothing resolved after the refused encounter. */
	CAMPAIGN_MISSION_APPLY_REFUSED = -1,

	/* Every encounter resolved to a map volume with no anchor left
	 * outstanding. */
	CAMPAIGN_MISSION_APPLY_OK = 0,

	/* Every volume resolved, but at least one anchored encounter
	 * awaits Finn's encounter association. */
	CAMPAIGN_MISSION_APPLY_PENDING_ANCHOR = 1,
};

struct campaign_mission_apply_report
{
	/* Encounters resolved with CAMPAIGN_MISSION_NO_ANCHOR. */
	int16_t resolved_count;

	/* Resolved encounters carrying an anchor: explicit unresolved
	 * until Finn's encounter association. */
	int16_t pending_count;

	/* Index of the first out-of-range encounter, or -1 when every
	 * volume resolved. */
	int16_t refused_index;
};

/* Pure resolution core: malformed records refuse before any volume is
 * touched; out-of-range volumes refuse at the first such encounter.
 * The report is filled on every return (including refusals) unless it
 * is NULL; a NULL mission refuses without touching the report. */
static inline int campaign_mission_resolve(
	const struct campaign_mission *mission,
	long trigger_volume_count,
	struct campaign_mission_apply_report *report)
{
	int16_t encounter;
	int16_t resolved = 0;
	int16_t pending = 0;

	if (!mission || !campaign_mission_valid(mission) || trigger_volume_count < 0)
		return CAMPAIGN_MISSION_APPLY_REFUSED;

	for (encounter = 0; encounter < mission->encounter_count; encounter++)
	{
		const struct campaign_mission_encounter *entry =
			&mission->encounters[encounter];

		if ((long)entry->trigger_volume_index >= trigger_volume_count)
		{
			if (report)
			{
				report->resolved_count = resolved;
				report->pending_count = pending;
				report->refused_index = encounter;
			}
			return CAMPAIGN_MISSION_APPLY_REFUSED;
		}
		if (entry->anchor_index != CAMPAIGN_MISSION_NO_ANCHOR)
			pending++;
		else
			resolved++;
	}

	if (report)
	{
		report->resolved_count = resolved;
		report->pending_count = pending;
		report->refused_index = -1;
	}
	return pending > 0 ?
		CAMPAIGN_MISSION_APPLY_PENDING_ANCHOR :
		CAMPAIGN_MISSION_APPLY_OK;
}

/* Engine entry: binds the loaded scenario's trigger_volumes block. A
 * NULL block is the no-scenario refusal. Finn's integration passes
 * &scenario->trigger_volumes. */
struct tag_block;
int campaign_mission_apply_volumes(
	const struct campaign_mission *mission,
	const struct tag_block *trigger_volumes,
	struct campaign_mission_apply_report *report);

#endif /* CAMPAIGN_MISSION_APPLY_H */
