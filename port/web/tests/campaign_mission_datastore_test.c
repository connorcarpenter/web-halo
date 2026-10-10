/* Hazard Run mission real-datastore acceptance (Vehicle A, fixture
 * correction slice).
 *
 * NATIVE-TOOLCHAIN ONLY. Do not build this file with the host gcc: it
 * compiles the UNMODIFIED source/game/campaign_mission.c, whose engine
 * headers (cseries/cseries.h, tag_files/files.h) need the native DAG
 * header closure, as in the sibling campaign_mission_author_test.c.
 *
 * Unlike the sibling author test, this unit defines NO backing store.
 * campaign_mission_save/load resolve to the REAL tag_files/files.o
 * datastore at link time, exercising the actual datastore
 * initialization/file-reference/storage contract. Test data is isolated
 * by filename: every case reads and writes ACCEPTANCE_FILE, which no
 * production path names, so authored "hazard_mission*" records are never
 * touched. Only deterministic store paths are asserted here -- a write
 * followed by its read, write-replaces semantics, and store-untouched
 * on a refused save. The missing-file read path is not asserted: the
 * production datastore_read leaves its result unassigned there
 * (preserved files.c behavior), so no stable expectation exists.
 *
 * Native DAG shape (tooling owner wires this): one TU .o per test .c;
 * link the real files.o providers plus exactly this control entrypoint.
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

#include "game/campaign_mission.c"

/* Isolated acceptance filename: never a production record name. */
#define ACCEPTANCE_FILE "hazard_mission_acceptance"

static int check_count = 0;
static int check_failures = 0;

#define CHECK(condition) do { \
	check_count++; \
	if (!(condition)) { \
		check_failures++; \
		fprintf(stderr, "CHECK FAILED line %d: %s\n", __LINE__, #condition); \
	} \
} while (0)

/* Real actor names from ai/actor_types.h (visible through the included
 * mission unit): grunt is hostile, player is refused by validation. */

static void acceptance_fill_mission(
	struct campaign_mission *mission,
	int16_t episode_index,
	const char *name_text,
	int16_t volume_index,
	int16_t actor_count)
{
	size_t length;

	memset(mission, 0, sizeof(*mission));
	mission->version = CAMPAIGN_MISSION_VERSION;
	mission->episode_index = episode_index;
	length = strlen(name_text);
	if (length >= CAMPAIGN_MISSION_NAME_CHARACTERS)
		length = CAMPAIGN_MISSION_NAME_CHARACTERS - 1;
	memcpy(mission->name, name_text, length);
	mission->encounter_count = 1;
	mission->encounters[0].trigger_volume_index = volume_index;
	mission->encounters[0].actor_type = _actor_grunt;
	mission->encounters[0].actor_count = actor_count;
	mission->encounters[0].anchor_index = CAMPAIGN_MISSION_NO_ANCHOR;
}

int main(void)
{
	struct campaign_mission written;
	struct campaign_mission reloaded;
	struct campaign_mission second;

	/* Write-then-read roundtrip through the real backing file. */
	acceptance_fill_mission(&written, 0, "Acceptance Alpha", 3, 5);
	CHECK(campaign_mission_save(ACCEPTANCE_FILE, &written) == true);
	memset(&reloaded, 0xA5, sizeof(reloaded));
	CHECK(campaign_mission_load(ACCEPTANCE_FILE, &reloaded) == true);
	CHECK(memcmp(&reloaded, &written, sizeof(written)) == 0);

	/* Write-replaces: a second save over the same file reads back latest. */
	acceptance_fill_mission(&second, 2, "Acceptance Beta", 7, 9);
	CHECK(campaign_mission_save(ACCEPTANCE_FILE, &second) == true);
	memset(&reloaded, 0xA5, sizeof(reloaded));
	CHECK(campaign_mission_load(ACCEPTANCE_FILE, &reloaded) == true);
	CHECK(memcmp(&reloaded, &second, sizeof(second)) == 0);

	/* A refused save leaves the stored record untouched. */
	second.encounters[0].actor_type = _actor_player;
	CHECK(campaign_mission_valid(&second) == false);
	CHECK(campaign_mission_save(ACCEPTANCE_FILE, &second) == false);
	acceptance_fill_mission(&second, 2, "Acceptance Beta", 7, 9);
	memset(&reloaded, 0xA5, sizeof(reloaded));
	CHECK(campaign_mission_load(ACCEPTANCE_FILE, &reloaded) == true);
	CHECK(memcmp(&reloaded, &second, sizeof(second)) == 0);

	if (check_failures == 0)
		printf("PASS campaign_mission_datastore_test: %d checks\n", check_count);
	else
		printf("FAIL campaign_mission_datastore_test: %d/%d checks failed\n",
			check_failures, check_count);
	return check_failures != 0;
}
