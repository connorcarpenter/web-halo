/* Campaign mission record save/reload/activate (Vehicle A, Hazard Run
 * authoring slice 1, play-path slice). Validation lives inline in
 * campaign_mission.h; this unit only moves valid records through the
 * datastore field and resolves them against the loaded scenario.
 * Store names live in campaign_mission.h so every per-episode address
 * spells the same field. */

#include "cseries/cseries.h"
#include "game/campaign_mission.h"
#include "game/campaign_mission_apply.h"
#include "game/campaign_mission_associate.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
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
	struct campaign_mission staged = {0};
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
	if (encounter_count > 0 &&
		(!trigger_volumes || !actor_types || !actor_counts || !anchors))
		return CAMPAIGN_MISSION_AUTHOR_BAD_ENCOUNTER;
	if (encounter_count < 0 || encounter_count > CAMPAIGN_MISSION_MAX_ENCOUNTERS)
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

bool campaign_mission_load_episode(
	int16_t episode_index,
	struct campaign_mission *mission)
{
	struct campaign_mission loaded;
	/* "hazard_mission_" plus one episode digit plus the terminator. */
	char field[sizeof(CAMPAIGN_MISSION_DATASTORE_FIELD_PREFIX) + 1];
	long prefix_length;

	if (episode_index < 0 ||
		episode_index >= CAMPAIGN_COOP_EPISODE_COUNT)
	{
		return false;
	}
	if (!mission)
		return false;

	for (prefix_length = 0; CAMPAIGN_MISSION_DATASTORE_FIELD_PREFIX[prefix_length] != '\0'; prefix_length++)
		field[prefix_length] = CAMPAIGN_MISSION_DATASTORE_FIELD_PREFIX[prefix_length];
	field[prefix_length] = (char)('0' + episode_index);
	field[prefix_length + 1] = '\0';

	if (!datastore_read(
		CAMPAIGN_MISSION_DATASTORE_FILE,
		field,
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

/* Loaded scenario's trigger_volumes block, or NULL when no scenario is
 * loaded. The layout-dependent member access lives here in the engine
 * unit; controls observe it through activate, never by arithmetic. */
static const struct tag_block *campaign_mission_scenario_volumes(
	void)
{
	const struct scenario *scenario = global_scenario_try_and_get();

	if (!scenario)
		return NULL;

	return &scenario->trigger_volumes;
}

/* Last mission record committed by a successful activation. */
static struct campaign_mission campaign_mission_activated_record;
static bool campaign_mission_has_activated = false;

int campaign_mission_activate(
	int16_t episode_index,
	struct campaign_mission_apply_report *report)
{
	struct campaign_mission record;
	const struct tag_block *volumes;
	int status;

	if (!campaign_mission_load_episode(episode_index, &record))
		return CAMPAIGN_MISSION_APPLY_REFUSED;

	volumes = campaign_mission_scenario_volumes();
	if (!volumes)
		return CAMPAIGN_MISSION_APPLY_REFUSED;

	status = campaign_mission_apply_volumes(&record, volumes, report);
	if (status != CAMPAIGN_MISSION_APPLY_OK)
		return status;

	campaign_mission_activated_record = record;
	campaign_mission_has_activated = true;

	return status;
}

const struct campaign_mission *campaign_mission_activated(
	void)
{
	if (!campaign_mission_has_activated)
		return NULL;

	return &campaign_mission_activated_record;
}

/* Last bound table committed by a fully bound activation. At most one
 * binding per encounter, so the record's encounter cap sizes it. */
static struct campaign_mission_binding campaign_mission_bound_bindings[
	CAMPAIGN_MISSION_MAX_ENCOUNTERS];
static int16_t campaign_mission_bound_count = 0;
static bool campaign_mission_has_bound = false;

int campaign_mission_activate_bound(
	int16_t episode_index,
	struct campaign_mission_apply_report *report,
	int16_t *bound_out)
{
	struct campaign_mission record;
	const struct scenario *scenario = global_scenario_try_and_get();
	const struct tag_block *volumes;
	struct campaign_mission_binding staged[
		CAMPAIGN_MISSION_MAX_ENCOUNTERS];
	int16_t bound = 0;
	int16_t entry;
	int status;

	if (!campaign_mission_load_episode(episode_index, &record))
	{
		if (bound_out)
			*bound_out = 0;
		return CAMPAIGN_MISSION_APPLY_REFUSED;
	}

	volumes = campaign_mission_scenario_volumes();
	if (!volumes)
	{
		if (bound_out)
			*bound_out = 0;
		return CAMPAIGN_MISSION_APPLY_REFUSED;
	}

	status = campaign_mission_apply_volumes(&record, volumes, report);
	if (status == CAMPAIGN_MISSION_APPLY_REFUSED)
	{
		if (bound_out)
			*bound_out = 0;
		return status;
	}

	/* OK and PENDING_ANCHOR both continue: the association resolves
	 * the pending half into bound-or-refused, never pending. */
	if (!scenario ||
		campaign_mission_associate(
			&record, scenario, staged,
			CAMPAIGN_MISSION_MAX_ENCOUNTERS, &bound) !=
		CAMPAIGN_MISSION_APPLY_OK)
	{
		if (bound_out)
			*bound_out = bound;
		return CAMPAIGN_MISSION_APPLY_REFUSED;
	}

	campaign_mission_activated_record = record;
	campaign_mission_has_activated = true;
	for (entry = 0; entry < bound; entry++)
		campaign_mission_bound_bindings[entry] = staged[entry];
	campaign_mission_bound_count = bound;
	campaign_mission_has_bound = true;

	if (bound_out)
		*bound_out = bound;
	return CAMPAIGN_MISSION_APPLY_OK;
}

const struct campaign_mission_binding *campaign_mission_bound_table(
	int16_t *count_out)
{
	if (!campaign_mission_has_bound)
		return NULL;

	if (count_out)
		*count_out = campaign_mission_bound_count;
	return campaign_mission_bound_bindings;
}
