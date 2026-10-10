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

	/* At most this many hostile encounters. Work-only missions use zero;
	 * their cooperative objectives execute in the GameMode guest. */
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

/* Engine-resolved mission store. The browser never names it: the commit
 * entry always writes here, so authored records land in one known file
 * with one field per episode. Shared with campaign_mission.c so every
 * per-episode address spells the same field. */
#define CAMPAIGN_MISSION_DATASTORE_FILE "hazard_missions"
#define CAMPAIGN_MISSION_DATASTORE_FIELD_PREFIX "hazard_mission_"

/* Legacy engine hostile classifications, pending original archetype binding.
 * Allies, neutrals, and props
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
	if (mission->encounter_count < 0 ||
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
	/* A serialized zero-hostile/work-only record cannot hide stale hostile
	 * rows beyond its count. Authoring always zeroes these unused rows. */
	for (; encounter < CAMPAIGN_MISSION_MAX_ENCOUNTERS; encounter++)
	{
		const struct campaign_mission_encounter *entry = &mission->encounters[encounter];
		if (entry->trigger_volume_index || entry->actor_type ||
			entry->actor_count || entry->anchor_index)
			return false;
	}

	return true;
}

/* Author-commit disposition, mirrored as plain ints by the web bridge
 * status poll. Zero is the only saved outcome; every other code leaves
 * the store untouched. */
enum campaign_mission_author_disposition
{
	CAMPAIGN_MISSION_AUTHOR_SAVED = 0,
	CAMPAIGN_MISSION_AUTHOR_BAD_EPISODE = 1,
	CAMPAIGN_MISSION_AUTHOR_BAD_NAME = 2,
	CAMPAIGN_MISSION_AUTHOR_BAD_ENCOUNTER = 3,
	/* Reserved former empty-draft code; zero encounters is valid. */
	CAMPAIGN_MISSION_AUTHOR_STORE_REFUSED = 5,
};

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

/* Browser-authoring commit. Assembles a record from flat scalar arrays
 * (the web bridge mailbox shape), runs the one canonical validator, and
 * stores it under the per-episode "hazard_mission_<episode>" field of the
 * engine-resolved mission file -- the caller never names the store, and
 * the browser holds no asset path. Values must fit int16_t; validation
 * applies after narrowing. Returns a campaign_mission_author_disposition;
 * on CAMPAIGN_MISSION_AUTHOR_SAVED the packed (episode << 8) | count
 * header is written to recorded_out (which may be NULL to skip it). */
int campaign_mission_author_commit(
	int16_t episode_index,
	const char mission_name[CAMPAIGN_MISSION_NAME_CHARACTERS],
	const int16_t trigger_volumes[CAMPAIGN_MISSION_MAX_ENCOUNTERS],
	const int16_t actor_types[CAMPAIGN_MISSION_MAX_ENCOUNTERS],
	const int16_t actor_counts[CAMPAIGN_MISSION_MAX_ENCOUNTERS],
	const int16_t anchors[CAMPAIGN_MISSION_MAX_ENCOUNTERS],
	int16_t encounter_count,
	int *recorded_out);

/* Loads the per-episode "hazard_mission_<episode>" field of the
 * engine-resolved mission file and validates it. The out-param is
 * untouched unless a valid record loads. */
bool campaign_mission_load_episode(
	int16_t episode_index,
	struct campaign_mission *mission);

/* Scenario resolver report (defined in game/campaign_mission_apply.h);
 * forward-declared so this header stays includable without tag types. */
struct campaign_mission_apply_report;

/* Activates the authored episode mission against the loaded scenario:
 * loads the episode record, resolves it against the scenario's
 * trigger_volumes, and commits the record for the encounter layer on
 * success. Returns a campaign_mission_apply status
 * (CAMPAIGN_MISSION_APPLY_OK / _REFUSED / _PENDING_ANCHOR, declared in
 * game/campaign_mission_apply.h). REFUSED and PENDING_ANCHOR leave any
 * prior activation untouched: an explicit anchor never silently becomes
 * the volume default. The report is filled by the resolver run; refusals
 * before any volume is touched (no record, no scenario) return REFUSED
 * with the report untouched. */
int campaign_mission_activate(
	int16_t episode_index,
	struct campaign_mission_apply_report *report);

/* Last activated mission record, or NULL before the first successful
 * activation. Read by the encounter layer; never mutated in place. */
const struct campaign_mission *campaign_mission_activated(
	void);

#endif /* CAMPAIGN_MISSION_H */
