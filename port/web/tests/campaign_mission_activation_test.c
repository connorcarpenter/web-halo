/* Hazard Run mission activation controls (Vehicle A, play-path slice).
 *
 * NATIVE-TOOLCHAIN ONLY. Do not build this file with the host gcc: it
 * compiles the UNMODIFIED source/game/campaign_mission.c, whose engine
 * headers need the native DAG header closure, as in the sibling
 * campaign_mission_author_test.c.
 *
 * TEST-ONLY backing store: the field-keyed store below lives under
 * test-only activation_datastore_write/activation_datastore_read names,
 * redirected from the production calls by macro, so this unit never
 * defines production datastore symbols and links clean beside the real
 * tag_files/files.o. The loaded scenario is the REAL engine global:
 * this control points global_scenario at a canned struct scenario (real
 * tag layout, native 32-bit TU) and restores NULL before returning, so
 * the production volume-block binding runs exactly as in the engine.
 * No engine function is redefined here.
 *
 * Covered: activation commits only on OK; REFUSED (no record, no
 * scenario, out-of-range volume, bad episode) and PENDING_ANCHOR leave
 * any prior activation untouched, so an explicit anchor never silently
 * becomes the volume default; per-episode loads read the field the
 * author commit wrote.
 *
 * Native DAG shape (tooling owner wires this): one TU .o per test .c;
 * link the real scenario/files providers plus exactly this control
 * entrypoint.
 */

#include <stdio.h>
#include <string.h>

/* The game-lib build puts port/linux/include/stdio.h first, which redirects
 * printf/fprintf to halo_linux_printf/halo_linux_fprintf (implemented in
 * msvc_crt.c, outside the controls link). Test reporting is not game
 * behavior, so use the host libc printers instead; production bytes are
 * unaffected. */
#undef printf
#undef fprintf

/* Redirect the production store calls inside the included unit to the
 * test-only names below. This TU then defines no datastore_write /
 * datastore_read symbols and cannot collide with tag_files/files.o. */
#define datastore_write activation_datastore_write
#define datastore_read activation_datastore_read

#include "game/campaign_mission.c"
#include "scenario/scenario_definitions.h"

#define ACTIVATION_STUB_MAX_FIELDS 8
#define ACTIVATION_STUB_FIELD_NAME_LIMIT 64
#define ACTIVATION_STUB_FIELD_DATA_LIMIT 256

static char activation_field_names[ACTIVATION_STUB_MAX_FIELDS][ACTIVATION_STUB_FIELD_NAME_LIMIT];
static unsigned char activation_field_data[ACTIVATION_STUB_MAX_FIELDS][ACTIVATION_STUB_FIELD_DATA_LIMIT];
static long activation_field_lengths[ACTIVATION_STUB_MAX_FIELDS];
static int activation_field_count = 0;
static int activation_write_count = 0;

static int activation_find_field(const char *field_name)
{
	int index;

	for (index = 0; index < activation_field_count; index++)
	{
		if (strncmp(
			activation_field_names[index],
			field_name,
			ACTIVATION_STUB_FIELD_NAME_LIMIT) == 0)
		{
			return index;
		}
	}

	return -1;
}

boolean activation_datastore_write(
	const char *file_name,
	const char *field_name,
	long length,
	const void *data)
{
	int index;

	(void)file_name;
	index = activation_find_field(field_name);
	if (index < 0)
	{
		if (activation_field_count >= ACTIVATION_STUB_MAX_FIELDS)
			return 0;
		index = activation_field_count++;
		strncpy(activation_field_names[index], field_name, ACTIVATION_STUB_FIELD_NAME_LIMIT - 1);
		activation_field_names[index][ACTIVATION_STUB_FIELD_NAME_LIMIT - 1] = '\0';
	}
	if (length < 0 || length > ACTIVATION_STUB_FIELD_DATA_LIMIT)
		return 0;
	memcpy(activation_field_data[index], data, (size_t)length);
	activation_field_lengths[index] = length;
	activation_write_count++;
	return 1;
}

boolean activation_datastore_read(
	const char *file_name,
	const char *field_name,
	long length,
	void *data)
{
	int index;

	(void)file_name;
	index = activation_find_field(field_name);
	if (index < 0)
		return 0;
	if (activation_field_lengths[index] != length)
		return 0;
	memcpy(data, activation_field_data[index], (size_t)length);
	return 1;
}

static int check_count = 0;
static int check_failures = 0;

#define CHECK(condition) do { \
	check_count++; \
	if (!(condition)) { \
		check_failures++; \
		fprintf(stderr, "CHECK FAILED line %d: %s\n", __LINE__, #condition); \
	} \
} while (0)

static void activation_fill_name(char *name, const char *text)
{
	size_t length = strlen(text);

	memset(name, 0, CAMPAIGN_MISSION_NAME_CHARACTERS);
	if (length >= CAMPAIGN_MISSION_NAME_CHARACTERS)
		length = CAMPAIGN_MISSION_NAME_CHARACTERS - 1;
	memcpy(name, text, length);
}

/* Commits one episode record through the real commit path. */
static int activation_commit_episode(
	int16_t episode,
	const char *name_text,
	int16_t volume,
	int16_t actor,
	int16_t actor_count,
	int16_t anchor)
{
	char name[CAMPAIGN_MISSION_NAME_CHARACTERS];
	int16_t volumes[CAMPAIGN_MISSION_MAX_ENCOUNTERS];
	int16_t actors[CAMPAIGN_MISSION_MAX_ENCOUNTERS];
	int16_t counts[CAMPAIGN_MISSION_MAX_ENCOUNTERS];
	int16_t anchors[CAMPAIGN_MISSION_MAX_ENCOUNTERS];
	int recorded = -1;

	memset(volumes, 0, sizeof(volumes));
	memset(actors, 0, sizeof(actors));
	memset(counts, 0, sizeof(counts));
	memset(anchors, 0, sizeof(anchors));
	activation_fill_name(name, name_text);
	volumes[0] = volume;
	actors[0] = actor;
	counts[0] = actor_count;
	anchors[0] = anchor;
	return campaign_mission_author_commit(
		episode, name, volumes, actors, counts, anchors, 1, &recorded);
}

int main(void)
{
	struct campaign_mission loaded;
	struct campaign_mission_apply_report report;
	static struct scenario canned_scenario;
	const struct campaign_mission *activated;
	int writes_before;

	memset(&canned_scenario, 0, sizeof(canned_scenario));
	canned_scenario.trigger_volumes.count = 4;

	/* Episode 0: two anchor-free encounters, both volumes in range. */
	CHECK(activation_commit_episode(0, "E1 Beachhead", 0, _actor_grunt, 4,
		CAMPAIGN_MISSION_NO_ANCHOR) == CAMPAIGN_MISSION_AUTHOR_SAVED);

	/* No scenario loaded: refusal before any mutation. */
	global_scenario = NULL;
	CHECK(campaign_mission_activated() == NULL);
	memset(&report, 0xA5, sizeof(report));
	CHECK(campaign_mission_activate(0, &report) == CAMPAIGN_MISSION_APPLY_REFUSED);
	CHECK(campaign_mission_activated() == NULL);
	CHECK(report.resolved_count == (int16_t)0xA5A5);
	CHECK(report.pending_count == (int16_t)0xA5A5);
	CHECK(report.refused_index == (int16_t)0xA5A5);

	/* Loaded scenario: OK commits the record for the encounter layer. */
	global_scenario = &canned_scenario;
	memset(&report, 0, sizeof(report));
	CHECK(campaign_mission_activate(0, &report) == CAMPAIGN_MISSION_APPLY_OK);
	CHECK(report.resolved_count == 1);
	CHECK(report.pending_count == 0);
	CHECK(report.refused_index == -1);
	activated = campaign_mission_activated();
	CHECK(activated != NULL);
	CHECK(activated->episode_index == 0);
	CHECK(activated->encounter_count == 1);

	/* Out-of-range volume: refusal, prior activation untouched. */
	CHECK(activation_commit_episode(1, "E2 High Ground", 9, _actor_elite, 2,
		CAMPAIGN_MISSION_NO_ANCHOR) == CAMPAIGN_MISSION_AUTHOR_SAVED);
	memset(&report, 0, sizeof(report));
	CHECK(campaign_mission_activate(1, &report) == CAMPAIGN_MISSION_APPLY_REFUSED);
	CHECK(report.refused_index == 0);
	activated = campaign_mission_activated();
	CHECK(activated != NULL);
	CHECK(activated->episode_index == 0);

	/* Anchored encounter with a valid volume: pending, never defaulted. */
	CHECK(activation_commit_episode(2, "E3 Anchor Hold", 1, _actor_jackal, 3,
		2) == CAMPAIGN_MISSION_AUTHOR_SAVED);
	memset(&report, 0, sizeof(report));
	CHECK(campaign_mission_activate(2, &report) == CAMPAIGN_MISSION_APPLY_PENDING_ANCHOR);
	CHECK(report.resolved_count == 0);
	CHECK(report.pending_count == 1);
	CHECK(report.refused_index == -1);
	activated = campaign_mission_activated();
	CHECK(activated != NULL);
	CHECK(activated->episode_index == 0);

	/* Bad episodes refuse without touching the store. */
	writes_before = activation_write_count;
	CHECK(campaign_mission_activate(-1, &report) == CAMPAIGN_MISSION_APPLY_REFUSED);
	CHECK(campaign_mission_activate(CAMPAIGN_COOP_EPISODE_COUNT, &report) ==
		CAMPAIGN_MISSION_APPLY_REFUSED);
	CHECK(activation_write_count == writes_before);

	/* Per-episode loads read the field the commit wrote. */
	memset(&loaded, 0xA5, sizeof(loaded));
	CHECK(campaign_mission_load_episode(0, &loaded) == true);
	CHECK(loaded.episode_index == 0);
	CHECK(campaign_mission_load_episode(9, &loaded) == false);
	memset(&loaded, 0xA5, sizeof(loaded));
	CHECK(campaign_mission_load_episode(5, &loaded) == false);
	CHECK(loaded.name[0] == (char)0xA5);

	global_scenario = NULL;

	if (check_failures == 0)
		printf("PASS campaign_mission_activation_test: %d checks\n", check_count);
	else
		printf("FAIL campaign_mission_activation_test: %d/%d checks failed\n",
			check_failures, check_count);
	return check_failures != 0;
}
