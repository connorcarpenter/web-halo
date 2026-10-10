/* Hazard Run encounter association controls (Vehicle A, play-path slice).
 *
 * NATIVE-TOOLCHAIN ONLY. Do not build this file with the host gcc: it
 * compiles the UNMODIFIED source/game/campaign_mission_associate.c,
 * whose engine headers need the native DAG header closure, as in the
 * sibling campaign_mission_activation_test.c.
 *
 * The loaded scenario is canned real tag layout (struct scenario with
 * encounter_definition rows and firing_position_definition feeds, native
 * 32-bit TU): the production block walk runs exactly as in the engine.
 * Records are built literally (no store); volume range is the
 * resolver's lane and is not exercised here. No engine function is
 * redefined here.
 *
 * Covered: positional encounter-table leg (authored N drives scenario
 * row N), anchor-to-firing-row leg, refusals (bad record, short
 * encounter table, anchor past the feeds, exhausted binding room) with
 * bound-so-far reporting, and anchor-free records binding nothing.
 *
 * Native DAG shape (tooling owner wires this): one TU .o per test .c;
 * link the real tag_files providers plus exactly this control
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

#include "game/campaign_mission_associate.c"
#include "scenario/scenario_definitions.h"
#include "ai/ai_scenario_definitions.h"

static int check_count = 0;
static int check_failures = 0;

#define CHECK(condition) do { \
	check_count++; \
	if (!(condition)) { \
		check_failures++; \
		fprintf(stderr, "CHECK FAILED line %d: %s\n", __LINE__, #condition); \
	} \
} while (0)

static void anchor_fill_name(char *name, const char *text)
{
	size_t length = strlen(text);

	memset(name, 0, CAMPAIGN_MISSION_NAME_CHARACTERS);
	if (length >= CAMPAIGN_MISSION_NAME_CHARACTERS)
		length = CAMPAIGN_MISSION_NAME_CHARACTERS - 1;
	memcpy(name, text, length);
}

/* Builds a valid record literally: volumes stay in range of nothing
 * here (the resolver owns that lane); all actors hostile grunts. */
static void anchor_fill_mission(
	struct campaign_mission *mission,
	int16_t encounter_count,
	const int16_t anchors[CAMPAIGN_MISSION_MAX_ENCOUNTERS])
{
	int16_t encounter;

	memset(mission, 0, sizeof(*mission));
	mission->version = CAMPAIGN_MISSION_VERSION;
	mission->episode_index = 0;
	anchor_fill_name(mission->name, "E1 Anchor Proof");
	mission->encounter_count = encounter_count;
	for (encounter = 0; encounter < encounter_count; encounter++)
	{
		mission->encounters[encounter].trigger_volume_index = encounter;
		mission->encounters[encounter].actor_type = _actor_grunt;
		mission->encounters[encounter].actor_count = 4;
		mission->encounters[encounter].anchor_index = anchors[encounter];
	}
}

int main(void)
{
	struct campaign_mission mission;
	static struct scenario canned_scenario;
	static struct encounter_definition canned_rows[2];
	static struct firing_position_definition canned_feeds[3];
	struct campaign_mission_binding bindings[CAMPAIGN_MISSION_MAX_ENCOUNTERS];
	int16_t anchors[CAMPAIGN_MISSION_MAX_ENCOUNTERS];
	int16_t bound = -1;
	int16_t encounter;

	for (encounter = 0; encounter < CAMPAIGN_MISSION_MAX_ENCOUNTERS; encounter++)
		anchors[encounter] = CAMPAIGN_MISSION_NO_ANCHOR;

	memset(&canned_scenario, 0, sizeof(canned_scenario));
	memset(canned_rows, 0, sizeof(canned_rows));
	memset(canned_feeds, 0, sizeof(canned_feeds));
	canned_rows[0].firing_positions.count = 3;
	canned_rows[0].firing_positions.address = canned_feeds;
	canned_rows[1].firing_positions.count = 1;
	canned_rows[1].firing_positions.address = canned_feeds;
	canned_scenario.ai_encounters.count = 2;
	canned_scenario.ai_encounters.address = canned_rows;

	/* Degenerate inputs refuse with no bindings. */
	memset(&mission, 0xA5, sizeof(mission));
	CHECK(campaign_mission_associate(NULL, &canned_scenario,
		bindings, 8, &bound) == CAMPAIGN_MISSION_APPLY_REFUSED);
	CHECK(bound == 0);
	anchor_fill_mission(&mission, 1, anchors);
	CHECK(campaign_mission_associate(&mission, NULL,
		bindings, 8, &bound) == CAMPAIGN_MISSION_APPLY_REFUSED);
	CHECK(bound == 0);
	memset(&mission, 0, sizeof(mission));
	CHECK(campaign_mission_associate(&mission, &canned_scenario,
		bindings, 8, &bound) == CAMPAIGN_MISSION_APPLY_REFUSED);
	CHECK(bound == 0);
	CHECK(campaign_mission_associate(&mission, &canned_scenario,
		bindings, 8, NULL) == CAMPAIGN_MISSION_APPLY_REFUSED);

	/* Anchor-free record: OK, nothing bound, table untouched. */
	anchor_fill_mission(&mission, 1, anchors);
	memset(bindings, 0xA5, sizeof(bindings));
	bound = -1;
	CHECK(campaign_mission_associate(&mission, &canned_scenario,
		bindings, 8, &bound) == CAMPAIGN_MISSION_APPLY_OK);
	CHECK(bound == 0);
	CHECK(bindings[0].encounter == (int16_t)0xA5A5);

	/* Anchored encounter binds its firing row. */
	anchors[0] = 2;
	anchor_fill_mission(&mission, 1, anchors);
	bound = -1;
	CHECK(campaign_mission_associate(&mission, &canned_scenario,
		bindings, 8, &bound) == CAMPAIGN_MISSION_APPLY_OK);
	CHECK(bound == 1);
	CHECK(bindings[0].encounter == 0);
	CHECK(bindings[0].firing_position == 2);

	/* Anchor past the feeds refuses with nothing bound. */
	anchors[0] = 3;
	anchor_fill_mission(&mission, 1, anchors);
	bound = -1;
	CHECK(campaign_mission_associate(&mission, &canned_scenario,
		bindings, 8, &bound) == CAMPAIGN_MISSION_APPLY_REFUSED);
	CHECK(bound == 0);

	/* Mixed table: the free encounter binds nothing, the anchored
	 * second encounter binds row 1's only feed. */
	anchors[0] = CAMPAIGN_MISSION_NO_ANCHOR;
	anchors[1] = 0;
	anchor_fill_mission(&mission, 2, anchors);
	bound = -1;
	CHECK(campaign_mission_associate(&mission, &canned_scenario,
		bindings, 8, &bound) == CAMPAIGN_MISSION_APPLY_OK);
	CHECK(bound == 1);
	CHECK(bindings[0].encounter == 1);
	CHECK(bindings[0].firing_position == 0);

	/* Second-encounter anchor past row 1's single feed refuses. */
	anchors[1] = 1;
	anchor_fill_mission(&mission, 2, anchors);
	bound = -1;
	CHECK(campaign_mission_associate(&mission, &canned_scenario,
		bindings, 8, &bound) == CAMPAIGN_MISSION_APPLY_REFUSED);
	CHECK(bound == 0);

	/* Partial table: the first bind stands, the count reports one. */
	anchors[0] = 1;
	anchors[1] = 5;
	anchor_fill_mission(&mission, 2, anchors);
	bound = -1;
	CHECK(campaign_mission_associate(&mission, &canned_scenario,
		bindings, 8, &bound) == CAMPAIGN_MISSION_APPLY_REFUSED);
	CHECK(bound == 1);
	CHECK(bindings[0].encounter == 0);
	CHECK(bindings[0].firing_position == 1);

	/* Authored position past the encounter table refuses. */
	anchors[0] = CAMPAIGN_MISSION_NO_ANCHOR;
	anchors[1] = CAMPAIGN_MISSION_NO_ANCHOR;
	anchors[2] = CAMPAIGN_MISSION_NO_ANCHOR;
	anchor_fill_mission(&mission, 3, anchors);
	bound = -1;
	CHECK(campaign_mission_associate(&mission, &canned_scenario,
		bindings, 8, &bound) == CAMPAIGN_MISSION_APPLY_REFUSED);
	CHECK(bound == 0);

	/* Exhausted room refuses before writing past capacity. */
	anchors[0] = 0;
	anchor_fill_mission(&mission, 1, anchors);
	memset(bindings, 0xA5, sizeof(bindings));
	bound = -1;
	CHECK(campaign_mission_associate(&mission, &canned_scenario,
		bindings, 0, &bound) == CAMPAIGN_MISSION_APPLY_REFUSED);
	CHECK(bound == 0);
	CHECK(bindings[0].encounter == (int16_t)0xA5A5);
	CHECK(campaign_mission_associate(&mission, &canned_scenario,
		NULL, 8, &bound) == CAMPAIGN_MISSION_APPLY_REFUSED);
	CHECK(campaign_mission_associate(&mission, &canned_scenario,
		bindings, 8, NULL) == CAMPAIGN_MISSION_APPLY_OK);

	/* Anchor-free record needs neither table rows nor bindings. */
	anchors[0] = CAMPAIGN_MISSION_NO_ANCHOR;
	anchor_fill_mission(&mission, 1, anchors);
	CHECK(campaign_mission_associate(&mission, &canned_scenario,
		NULL, 0, &bound) == CAMPAIGN_MISSION_APPLY_OK);
	CHECK(bound == 0);

	if (check_failures == 0)
		printf("PASS campaign_mission_anchor_test: %d checks\n", check_count);
	else
		printf("FAIL campaign_mission_anchor_test: %d/%d checks failed\n",
			check_failures, check_count);
	return check_failures != 0;
}
