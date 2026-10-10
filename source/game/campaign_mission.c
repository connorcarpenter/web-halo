/* Campaign mission record save/reload (Vehicle A, Hazard Run authoring
 * slice 1). Validation lives inline in campaign_mission.h; this unit
 * only moves valid records through the datastore field. */

#include "cseries/cseries.h"
#include "game/campaign_mission.h"
#include "tag_files/files.h"

/* Datastore field holding the flat record verbatim. */
#define CAMPAIGN_MISSION_DATASTORE_FIELD "hazard_mission"

bool campaign_mission_save(
	const char *file_name,
	const struct campaign_mission *mission)
{
	if (!file_name || !mission)
		return false;
	if (!campaign_mission_valid(mission))
		return false;

	return datastore_write(
		file_name,
		CAMPAIGN_MISSION_DATASTORE_FIELD,
		(long)sizeof(*mission),
		mission) ? true : false;
}

bool campaign_mission_load(
	const char *file_name,
	struct campaign_mission *mission)
{
	struct campaign_mission loaded;

	if (!file_name || !mission)
		return false;
	if (!datastore_read(
		file_name,
		CAMPAIGN_MISSION_DATASTORE_FIELD,
		(long)sizeof(loaded),
		&loaded))
	{
		return false;
	}
	if (!campaign_mission_valid(&loaded))
		return false;

	*mission = loaded;

	return true;
}
