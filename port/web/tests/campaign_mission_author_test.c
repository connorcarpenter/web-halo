/* Hazard Run mission authoring commit (Vehicle A, authoring slice 2).
 *
 * NATIVE-TOOLCHAIN ONLY. Do not build this file with the host gcc: it
 * compiles the UNMODIFIED source/game/campaign_mission.c, whose engine
 * headers (cseries/cseries.h, tag_files/files.h) need the native DAG
 * header closure, as in the sibling campaign_coop_native_controls_test.c.
 * Only the backing store is stubbed at link level -- the field-keyed
 * datastore_write/datastore_read below -- so per-episode separation and
 * store-untouched-on-refusal are actually verified against the real
 * commit path, including the NULL-array refusal.
 *
 * Native DAG shape (tooling owner wires this): one TU .o per test .c. */

#include <stdio.h>
#include <string.h>

/* The game-lib build puts port/linux/include/stdio.h first, which redirects
 * printf/fprintf to halo_linux_printf/halo_linux_fprintf (implemented in
 * msvc_crt.c, outside the controls link). Test reporting is not game
 * behavior, so use the host libc printers instead; production bytes are
 * unaffected. */
#undef printf
#undef fprintf

#include "game/campaign_mission.c"

/* cseries.h maps libc string/mem calls to engine cs* names (csstrncmp,
 * csmemcpy, ...) implemented in cseries.c, outside the controls link.
 * Fixture helpers are not game behavior, so bind the ones this TU uses
 * back to libc; production bytes are unaffected. */
#undef memcmp
#undef memset
#undef strncmp
#undef strncpy
#undef strlen
#undef memcpy

#define AUTHOR_STUB_MAX_FIELDS 8
#define AUTHOR_STUB_FIELD_NAME_LIMIT 64
#define AUTHOR_STUB_FIELD_DATA_LIMIT 256

static char stub_field_names[AUTHOR_STUB_MAX_FIELDS][AUTHOR_STUB_FIELD_NAME_LIMIT];
static unsigned char stub_field_data[AUTHOR_STUB_MAX_FIELDS][AUTHOR_STUB_FIELD_DATA_LIMIT];
static long stub_field_lengths[AUTHOR_STUB_MAX_FIELDS];
static int stub_field_count = 0;
static int stub_write_count = 0;
static int stub_fail_writes = 0;

static int stub_find_field(const char *field_name)
{
	int index;

	for (index = 0; index < stub_field_count; index++)
	{
		if (strncmp(
			stub_field_names[index],
			field_name,
			AUTHOR_STUB_FIELD_NAME_LIMIT) == 0)
		{
			return index;
		}
	}

	return -1;
}

boolean datastore_write(
	const char *file_name,
	const char *field_name,
	long length,
	const void *data)
{
	int index;

	(void)file_name;
	if (stub_fail_writes)
		return 0;
	index = stub_find_field(field_name);
	if (index < 0)
	{
		if (stub_field_count >= AUTHOR_STUB_MAX_FIELDS)
			return 0;
		index = stub_field_count++;
		strncpy(stub_field_names[index], field_name, AUTHOR_STUB_FIELD_NAME_LIMIT - 1);
		stub_field_names[index][AUTHOR_STUB_FIELD_NAME_LIMIT - 1] = '\0';
	}
	if (length < 0 || length > AUTHOR_STUB_FIELD_DATA_LIMIT)
		return 0;
	memcpy(stub_field_data[index], data, (size_t)length);
	stub_field_lengths[index] = length;
	stub_write_count++;
	return 1;
}

boolean datastore_read(
	const char *file_name,
	const char *field_name,
	long length,
	void *data)
{
	int index;

	(void)file_name;
	index = stub_find_field(field_name);
	if (index < 0)
		return 0;
	if (stub_field_lengths[index] != length)
		return 0;
	memcpy(data, stub_field_data[index], (size_t)length);
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

/* Grunt-like and infection-form-like hostiles; player is never hostile. */
#define AUTHOR_TEST_GRUNT 2
#define AUTHOR_TEST_INFECTION_FORM 10
#define AUTHOR_TEST_PLAYER 6

static void author_test_fill_name(char *name, const char *text)
{
	size_t length = strlen(text);

	memset(name, 0, CAMPAIGN_MISSION_NAME_CHARACTERS);
	if (length >= CAMPAIGN_MISSION_NAME_CHARACTERS)
		length = CAMPAIGN_MISSION_NAME_CHARACTERS - 1;
	memcpy(name, text, length);
}

int main(void)
{
	char name[CAMPAIGN_MISSION_NAME_CHARACTERS];
	int16_t volumes[CAMPAIGN_MISSION_MAX_ENCOUNTERS];
	int16_t actors[CAMPAIGN_MISSION_MAX_ENCOUNTERS];
	int16_t counts[CAMPAIGN_MISSION_MAX_ENCOUNTERS];
	int16_t anchors[CAMPAIGN_MISSION_MAX_ENCOUNTERS];
	struct campaign_mission widened;
	unsigned char episode_one_snapshot[sizeof(struct campaign_mission)];
	int recorded;
	int writes_before;
	int disposition;

	memset(volumes, 0, sizeof(volumes));
	memset(actors, 0, sizeof(actors));
	memset(counts, 0, sizeof(counts));
	memset(anchors, 0, sizeof(anchors));

	/* Green commit on episode 1 with two hostile encounters. */
	author_test_fill_name(name, "E1 Canyon Watch");
	volumes[0] = 4;
	actors[0] = AUTHOR_TEST_GRUNT;
	counts[0] = 6;
	anchors[0] = -1;
	volumes[1] = 9;
	actors[1] = AUTHOR_TEST_INFECTION_FORM;
	counts[1] = 12;
	anchors[1] = 2;
	recorded = -1;
	disposition = campaign_mission_author_commit(
		1, name, volumes, actors, counts, anchors, 2, &recorded);
	CHECK(disposition == CAMPAIGN_MISSION_AUTHOR_SAVED);
	CHECK(recorded == ((1 << 8) | 2));
	CHECK(stub_write_count == 1);
	CHECK(stub_find_field("hazard_mission_1") >= 0);
	memcpy(&widened, stub_field_data[stub_find_field("hazard_mission_1")], sizeof(widened));
	CHECK(campaign_mission_valid(&widened));
	CHECK(widened.episode_index == 1);
	CHECK(widened.encounter_count == 2);
	CHECK(memcmp(widened.name, name, CAMPAIGN_MISSION_NAME_CHARACTERS) == 0);
	memcpy(episode_one_snapshot, &widened, sizeof(widened));

	/* Per-episode separation: episode 3 lands beside episode 1. */
	author_test_fill_name(name, "E3 Flood Gate");
	volumes[0] = 1;
	actors[0] = AUTHOR_TEST_INFECTION_FORM;
	counts[0] = 4;
	anchors[0] = -1;
	disposition = campaign_mission_author_commit(
		3, name, volumes, actors, counts, anchors, 1, &recorded);
	CHECK(disposition == CAMPAIGN_MISSION_AUTHOR_SAVED);
	CHECK(recorded == ((3 << 8) | 1));
	memcpy(&widened, stub_field_data[stub_find_field("hazard_mission_1")], sizeof(widened));
	CHECK(memcmp(&widened, episode_one_snapshot, sizeof(widened)) == 0);
	memcpy(&widened, stub_field_data[stub_find_field("hazard_mission_3")], sizeof(widened));
	CHECK(campaign_mission_valid(&widened));

	/* Bad episodes refuse with the store untouched. */
	writes_before = stub_write_count;
	disposition = campaign_mission_author_commit(
		-1, name, volumes, actors, counts, anchors, 1, &recorded);
	CHECK(disposition == CAMPAIGN_MISSION_AUTHOR_BAD_EPISODE);
	disposition = campaign_mission_author_commit(
		8, name, volumes, actors, counts, anchors, 1, &recorded);
	CHECK(disposition == CAMPAIGN_MISSION_AUTHOR_BAD_EPISODE);
	CHECK(stub_write_count == writes_before);

	/* Empty and NULL names refuse. */
	memset(name, 0, sizeof(name));
	disposition = campaign_mission_author_commit(
		1, name, volumes, actors, counts, anchors, 1, &recorded);
	CHECK(disposition == CAMPAIGN_MISSION_AUTHOR_BAD_NAME);
	disposition = campaign_mission_author_commit(
		1, NULL, volumes, actors, counts, anchors, 1, &recorded);
	CHECK(disposition == CAMPAIGN_MISSION_AUTHOR_BAD_NAME);
	CHECK(stub_write_count == writes_before);

	/* Bad encounters refuse: non-hostile, counts, volume, anchor, NULL array. */
	author_test_fill_name(name, "E1 Canyon Watch");
	actors[0] = AUTHOR_TEST_PLAYER;
	disposition = campaign_mission_author_commit(
		1, name, volumes, actors, counts, anchors, 1, &recorded);
	CHECK(disposition == CAMPAIGN_MISSION_AUTHOR_BAD_ENCOUNTER);
	actors[0] = AUTHOR_TEST_GRUNT;
	counts[0] = 0;
	disposition = campaign_mission_author_commit(
		1, name, volumes, actors, counts, anchors, 1, &recorded);
	CHECK(disposition == CAMPAIGN_MISSION_AUTHOR_BAD_ENCOUNTER);
	counts[0] = 17;
	disposition = campaign_mission_author_commit(
		1, name, volumes, actors, counts, anchors, 1, &recorded);
	CHECK(disposition == CAMPAIGN_MISSION_AUTHOR_BAD_ENCOUNTER);
	counts[0] = 9;
	disposition = campaign_mission_author_commit(
		1, name, volumes, actors, counts, anchors, 9, &recorded);
	CHECK(disposition == CAMPAIGN_MISSION_AUTHOR_BAD_ENCOUNTER);
	volumes[0] = -1;
	disposition = campaign_mission_author_commit(
		1, name, volumes, actors, counts, anchors, 1, &recorded);
	CHECK(disposition == CAMPAIGN_MISSION_AUTHOR_BAD_ENCOUNTER);
	volumes[0] = 4;
	anchors[0] = -2;
	disposition = campaign_mission_author_commit(
		1, name, volumes, actors, counts, anchors, 1, &recorded);
	CHECK(disposition == CAMPAIGN_MISSION_AUTHOR_BAD_ENCOUNTER);
	anchors[0] = -1;
	disposition = campaign_mission_author_commit(
		1, name, volumes, NULL, counts, anchors, 1, &recorded);
	CHECK(disposition == CAMPAIGN_MISSION_AUTHOR_BAD_ENCOUNTER);
	CHECK(stub_write_count == writes_before);

	/* Empty draft refuses. */
	disposition = campaign_mission_author_commit(
		1, name, volumes, actors, counts, anchors, 0, &recorded);
	CHECK(disposition == CAMPAIGN_MISSION_AUTHOR_EMPTY_DRAFT);
	CHECK(stub_write_count == writes_before);

	/* NULL recorded_out still saves on a green commit. */
	disposition = campaign_mission_author_commit(
		2, name, volumes, actors, counts, anchors, 1, NULL);
	CHECK(disposition == CAMPAIGN_MISSION_AUTHOR_SAVED);
	memcpy(&widened, stub_field_data[stub_find_field("hazard_mission_2")], sizeof(widened));
	CHECK(campaign_mission_valid(&widened));

	/* Store failure surfaces without touching the recorded proof. */
	stub_fail_writes = 1;
	recorded = -7;
	disposition = campaign_mission_author_commit(
		2, name, volumes, actors, counts, anchors, 1, &recorded);
	CHECK(disposition == CAMPAIGN_MISSION_AUTHOR_STORE_REFUSED);
	CHECK(recorded == -7);
	stub_fail_writes = 0;
	disposition = campaign_mission_author_commit(
		2, name, volumes, actors, counts, anchors, 1, &recorded);
	CHECK(disposition == CAMPAIGN_MISSION_AUTHOR_SAVED);

	if (check_failures == 0)
		printf("PASS campaign_mission_author_test: %d checks\n", check_count);
	else
		printf("FAIL campaign_mission_author_test: %d/%d checks failed\n",
			check_failures, check_count);
	return check_failures != 0;
}
