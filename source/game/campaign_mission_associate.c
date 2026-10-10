/* Campaign mission encounter association, engine binding (Vehicle A,
 * Hazard Run play-path slice, Finn). The binding contract lives in
 * game/campaign_mission_associate.h; this unit only walks the loaded
 * scenario's ai_encounters table beside the record. Volume range stays
 * Roger's lane: call after the resolver reports OK or PENDING_ANCHOR. */

#include "cseries/cseries.h"
#include "game/campaign_mission.h"
#include "game/campaign_mission_associate.h"
#include "game/campaign_mission_apply.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "ai/ai_scenario_definitions.h"
#include "tag_files/tag_groups.h"

int campaign_mission_associate(
	const struct campaign_mission *mission,
	const struct scenario *scenario,
	struct campaign_mission_binding *bindings,
	int16_t binding_capacity,
	int16_t *bound_out)
{
	int16_t encounter;
	int16_t bound = 0;

	if (!mission || !campaign_mission_valid(mission))
	{
		if (bound_out)
			*bound_out = 0;
		return CAMPAIGN_MISSION_APPLY_REFUSED;
	}
	if (!scenario || scenario->ai_encounters.count < 0)
	{
		if (bound_out)
			*bound_out = 0;
		return CAMPAIGN_MISSION_APPLY_REFUSED;
	}

	for (encounter = 0; encounter < mission->encounter_count; encounter++)
	{
		const struct campaign_mission_encounter *entry =
			&mission->encounters[encounter];
		const struct encounter_definition *scenario_encounter;

		/* Positional encounter-table leg: authored encounter N drives
		 * scenario ai_encounters row N. */
		if ((long)encounter >= scenario->ai_encounters.count)
		{
			if (bound_out)
				*bound_out = bound;
			return CAMPAIGN_MISSION_APPLY_REFUSED;
		}
		if (entry->anchor_index == CAMPAIGN_MISSION_NO_ANCHOR)
			continue;

		scenario_encounter = TAG_BLOCK_GET_ELEMENT(
			&scenario->ai_encounters,
			(long)encounter,
			struct encounter_definition);
		if ((long)entry->anchor_index >=
			scenario_encounter->firing_positions.count)
		{
			if (bound_out)
				*bound_out = bound;
			return CAMPAIGN_MISSION_APPLY_REFUSED;
		}
		if (!bindings || (long)bound >= (long)binding_capacity)
		{
			if (bound_out)
				*bound_out = bound;
			return CAMPAIGN_MISSION_APPLY_REFUSED;
		}
		bindings[bound].encounter = encounter;
		bindings[bound].firing_position = entry->anchor_index;
		bound++;
	}

	if (bound_out)
		*bound_out = bound;
	return CAMPAIGN_MISSION_APPLY_OK;
}
