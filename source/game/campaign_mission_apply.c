/* Campaign mission scenario resolver, engine binding (Vehicle A,
 * Hazard Run play-path slice, Roger). The resolution core is inline in
 * campaign_mission_apply.h; this unit only binds the loaded
 * scenario's trigger_volumes block (NULL block = no-scenario refusal)
 * and forwards its count. */

#include "cseries/cseries.h"
#include "game/campaign_mission_apply.h"
#include "tag_files/tag_groups.h"

int campaign_mission_apply_volumes(
	const struct campaign_mission *mission,
	const struct tag_block *trigger_volumes,
	struct campaign_mission_apply_report *report)
{
	if (!mission || !trigger_volumes)
		return CAMPAIGN_MISSION_APPLY_REFUSED;
	return campaign_mission_resolve(mission, trigger_volumes->count, report);
}
