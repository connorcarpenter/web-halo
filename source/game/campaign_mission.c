/* Campaign mission record save/reload (Vehicle A, Hazard Run authoring
 * slice 1). Validation lives inline in campaign_mission.h; this unit
 * only moves valid records through the datastore field. */

#include "cseries/cseries.h"
#include "game/campaign_mission.h"
#include "tag_files/files.h"

/* Datastore field holding the flat record verbatim. */
#define CAMPAIGN_MISSION_DATASTORE_FIELD "hazard_mission"

/* Engine-resolved mission store. The browser never names it: the commit
 * entry below always writes here, so authored records land in one known
 * file with one field per episode. */
#define CAMPAIGN_MISSION_DATASTORE_FILE "hazard_missions"
#define CAMPAIGN_MISSION_DATASTORE_FIELD_PREFIX "hazard_mission_"

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

int campaign_mission_author_commit(
	int16_t episode_index,
	const char mission_name[CAMPAIGN_MISSION_NAME_CHARACTERS],
	const int16_t trigger_volumes[CAMPAIGN_MISSION_MAX_ENCOUNTERS],
	const int16_t actor_types[CAMPAIGN_MISSION_MAX_ENCOUNTERS],
	const int16_t actor_counts[CAMPAIGN_MISSION_MAX_ENCOUNTERS],
	const int16_t anchors[CAMPAIGN_MISSION_MAX_ENCOUNTERS],
	int16_t encounter_count,
	int *recorded_out)
{
	struct campaign_mission staged;
	/* "hazard_mission_" plus one episode digit plus the terminator. */
	char field[sizeof(CAMPAIGN_MISSION_DATASTORE_FIELD_PREFIX) + 1];
	int16_t encounter;
	long prefix_length;

	if (episode_index < 0 ||
		episode_index >= CAMPAIGN_COOP_EPISODE_COUNT)
	{
		return CAMPAIGN_MISSION_AUTHOR_BAD_EPISODE;
	}
	if (!mission_name || mission_name[0] == '\0')
		return CAMPAIGN_MISSION_AUTHOR_BAD_NAME;
	if (!trigger_volumes || !actor_types || !actor_counts || !anchors)
		return CAMPAIGN_MISSION_AUTHOR_BAD_ENCOUNTER;
	if (encounter_count <= 0)
		return CAMPAIGN_MISSION_AUTHOR_EMPTY_DRAFT;
	if (encounter_count > CAMPAIGN_MISSION_MAX_ENCOUNTERS)
		return CAMPAIGN_MISSION_AUTHOR_BAD_ENCOUNTER;

	staged.version = CAMPAIGN_MISSION_VERSION;
	staged.episode_index = episode_index;
	for (encounter = 0; encounter < CAMPAIGN_MISSION_NAME_CHARACTERS; encounter++)
		staged.name[encounter] = mission_name[encounter];
	staged.encounter_count = encounter_count;
	for (encounter = 0; encounter < encounter_count; encounter++)
	{
		staged.encounters[encounter].trigger_volume_index = trigger_volumes[encounter];
		staged.encounters[encounter].actor_type = actor_types[encounter];
		staged.encounters[encounter].actor_count = actor_counts[encounter];
		staged.encounters[encounter].anchor_index = anchors[encounter];
	}

	if (!campaign_mission_valid(&staged))
	{
		if (staged.name[0] == '\0')
			return CAMPAIGN_MISSION_AUTHOR_BAD_NAME;
		return CAMPAIGN_MISSION_AUTHOR_BAD_ENCOUNTER;
	}

	for (prefix_length = 0; CAMPAIGN_MISSION_DATASTORE_FIELD_PREFIX[prefix_length] != '\0'; prefix_length++)
		field[prefix_length] = CAMPAIGN_MISSION_DATASTORE_FIELD_PREFIX[prefix_length];
	field[prefix_length] = (char)('0' + episode_index);
	field[prefix_length + 1] = '\0';

	if (!datastore_write(
		CAMPAIGN_MISSION_DATASTORE_FILE,
		field,
		(long)sizeof(staged),
		&staged))
	{
		return CAMPAIGN_MISSION_AUTHOR_STORE_REFUSED;
	}

	if (recorded_out)
		*recorded_out = ((int)episode_index << 8) | (int)encounter_count;

	return CAMPAIGN_MISSION_AUTHOR_SAVED;
}
