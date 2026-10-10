/* Campaign mission record (Vehicle A, Hazard Run authoring slice 1).
 *
 * A mission is authored data, not code: episode + encounters over the
 * stock scenario schema (trigger volume indices into the map's
 * trigger_volumes, actor types from source/ai/actor_types.h). Records are
 * validated, saved, and reloaded through this unit; applying a record to
 * the loaded scenario is the play-path follow-on, which resolves volume
 * indices against the map's actual data.
 *
 * The validation core is static inline so the exact bytes run in the
 * engine and in the standalone formation test. Structs are flat
 * (int16_t and char only, no pointers) so a record is a
 * datastore field verbatim.
 */

#ifndef CAMPAIGN_MISSION_H
#define CAMPAIGN_MISSION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ai/actor_types.h"
#include "game/campaign_coop.h"

enum
{
	/* Mission record version; bump when the struct changes. */
	CAMPAIGN_MISSION_VERSION = 1,

	/* Corridor-to-room episode shape (E1 shipboard default): at most this
	 * many encounters per mission. */
	CAMPAIGN_MISSION_MAX_ENCOUNTERS = 8,

	/* Room-scale encounter cap. */
	CAMPAIGN_MISSION_MAX_ACTORS_PER_ENCOUNTER = 16,

	CAMPAIGN_MISSION_NAME_CHARACTERS = 32,

	/* No anchor: the encounter spawns at its trigger volume default. */
	CAMPAIGN_MISSION_NO_ANCHOR = -1,
};

struct campaign_mission_encounter
{
	/* Index into the map scenario's trigger_volumes; resolved against
	 * the actual map data at apply time, must be non-negative here. */
	int16_t trigger_volume_index;

	/* One of the Hazard Run hostile actor types (see below). */
	int16_t actor_type;

	/* 1..CAMPAIGN_MISSION_MAX_ACTORS_PER_ENCOUNTER. */
	int16_t actor_count;

	/* Firing-position anchor, or CAMPAIGN_MISSION_NO_ANCHOR. */
	int16_t anchor_index;
};

struct campaign_mission
{
	int16_t version;
	int16_t episode_index;
	char name[CAMPAIGN_MISSION_NAME_CHARACTERS];
	int16_t encounter_count;
	struct campaign_mission_encounter encounters[CAMPAIGN_MISSION_MAX_ENCOUNTERS];
};

/* Hazard Run hostiles: covenant boarders for the shipboard episodes and
 * swarm forms for the later ones. Allies, neutrals, and props
 * (player/marine/crew/monitor/sentinel/mounted_weapon/engineer/assassin)
 * are refused by mode validation. Static inline with the validator so
 * both stay the same bytes everywhere. */
static inline bool campaign_mission_actor_hostile(
	int16_t actor_type)
{
	switch (actor_type)
	{
	case _actor_elite:
	case _actor_jackal:
	case _actor_grunt:
	case _actor_hunter:
	case _actor_combat_form:
	case _actor_infection_form:
	case _actor_carrier_form:
		return true;
	default:
		return false;
	}
}

/* Pure mission core: version, episode range (shared with the host
 * handoff), non-empty name, encounter count, and every encounter's
 * volume/actor/count/anchor. Refusals mean unauthored data, never a
 * partial mission. */
static inline bool campaign_mission_valid(
	const struct campaign_mission *mission)
{
	int16_t encounter;

	if (!mission)
		return false;
	if (mission->version != CAMPAIGN_MISSION_VERSION)
		return false;
	if (mission->episode_index < 0 ||
		mission->episode_index >= CAMPAIGN_COOP_EPISODE_COUNT)
		return false;
	if (mission->name[0] == '\0')
		return false;
	if (mission->encounter_count <= 0 ||
		mission->encounter_count > CAMPAIGN_MISSION_MAX_ENCOUNTERS)
		return false;

	for (encounter = 0; encounter < mission->encounter_count; encounter++)
	{
		const struct campaign_mission_encounter *entry = &mission->encounters[encounter];

		if (entry->trigger_volume_index < 0)
			return false;
		if (!campaign_mission_actor_hostile(entry->actor_type))
			return false;
		if (entry->actor_count <= 0 ||
			entry->actor_count > CAMPAIGN_MISSION_MAX_ACTORS_PER_ENCOUNTER)
			return false;
		if (entry->anchor_index < CAMPAIGN_MISSION_NO_ANCHOR)
			return false;
	}

	return true;
}

/* Validates, then stores the record under the "hazard_mission" field of
 * file_name. Refuses invalid records; the store is untouched then. */
bool campaign_mission_save(
	const char *file_name,
	const struct campaign_mission *mission);

/* Loads the "hazard_mission" field of file_name and validates it. The
 * out-param is untouched unless a valid record loads. */
bool campaign_mission_load(
	const char *file_name,
	struct campaign_mission *mission);

#endif /* CAMPAIGN_MISSION_H */
