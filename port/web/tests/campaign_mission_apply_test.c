/* Hazard Run mission scenario-resolver controls (Vehicle A, play-path
 * slice, Roger).
 *
 * Header-only standalone build: exercises the exact resolution bytes the
 * engine runs (static inline in source/game/campaign_mission_apply.h),
 * with no engine link. The tag_block-bound engine entry in
 * source/game/campaign_mission_apply.c is not compiled here: it includes
 * engine headers carrying a 32-bit layout assertion that cannot pass
 * under the host 64-bit gcc, so its compile belongs to the native
 * (-m32) route, not this test. Run with:
 * gcc -Isource -o /tmp/campaign_mission_apply_test \
 *   port/web/tests/campaign_mission_apply_test.c \
 *   && /tmp/campaign_mission_apply_test
 */

#include <stdio.h>
#include <string.h>

#include "game/campaign_coop.h"

/* Engine scalar contract for the mission header's actor enum: ai/actor_types.h
 * spells later fields with these Xbox ABI types (byte/boolean), which the
 * engine build takes from cseries/cseries.h. The standalone test has no
 * engine link and must not pull the port stdio redirect, so it states the
 * stable ABI spellings directly; the enum VALUES and resolver logic under
 * test stay the exact header bytes. */
typedef unsigned char byte;
typedef unsigned char boolean;
#include "game/campaign_mission.h"
#include "game/campaign_mission_apply.h"

static int checks_run;

#define CHECK(condition) \
	do { \
		checks_run++; \
		if (!(condition)) \
		{ \
			printf("FAIL line %d: %s\n", __LINE__, #condition); \
			return 1; \
		} \
	} while (0)

static void make_mission(
	struct campaign_mission *mission)
{
	memset(mission, 0, sizeof(*mission));
	mission->version = CAMPAIGN_MISSION_VERSION;
	mission->episode_index = 0;
	mission->name[0] = 'E';
	mission->name[1] = '1';
	mission->encounter_count = 2;
	mission->encounters[0].trigger_volume_index = 0;
	mission->encounters[0].actor_type = _actor_grunt;
	mission->encounters[0].actor_count = 4;
	mission->encounters[0].anchor_index = CAMPAIGN_MISSION_NO_ANCHOR;
	mission->encounters[1].trigger_volume_index = 3;
	mission->encounters[1].actor_type = _actor_elite;
	mission->encounters[1].actor_count = 2;
	mission->encounters[1].anchor_index = CAMPAIGN_MISSION_NO_ANCHOR;
}

int main(
	void)
{
	struct campaign_mission mission;
	struct campaign_mission_apply_report report;

	/* POSITIVE: every volume in range resolves, nothing pending. */
	make_mission(&mission);
	CHECK(campaign_mission_resolve(&mission, 4, &report) ==
		CAMPAIGN_MISSION_APPLY_OK);
	CHECK(report.resolved_count == 2);
	CHECK(report.pending_count == 0);
	CHECK(report.refused_index == -1);

	/* POSITIVE boundary: last valid index is count - 1. */
	make_mission(&mission);
	CHECK(campaign_mission_resolve(&mission, 4, &report) ==
		CAMPAIGN_MISSION_APPLY_OK);

	/* ANCHOR: a resolved volume with an anchor stays explicitly
	 * unresolved until Finn's encounter association consumes it. */
	make_mission(&mission);
	mission.encounters[1].anchor_index = 7;
	CHECK(campaign_mission_resolve(&mission, 4, &report) ==
		CAMPAIGN_MISSION_APPLY_PENDING_ANCHOR);
	CHECK(report.resolved_count == 1);
	CHECK(report.pending_count == 1);
	CHECK(report.refused_index == -1);

	/* ANCHOR-ONLY: no free encounter still reports pending, not ok. */
	make_mission(&mission);
	mission.encounters[0].anchor_index = 5;
	mission.encounters[1].anchor_index = 7;
	CHECK(campaign_mission_resolve(&mission, 4, &report) ==
		CAMPAIGN_MISSION_APPLY_PENDING_ANCHOR);
	CHECK(report.resolved_count == 0);
	CHECK(report.pending_count == 2);

	/* REFUSAL: first out-of-range volume refuses with its index; the
	 * earlier resolved encounter is still counted. */
	make_mission(&mission);
	mission.encounters[1].trigger_volume_index = 4;
	CHECK(campaign_mission_resolve(&mission, 4, &report) ==
		CAMPAIGN_MISSION_APPLY_REFUSED);
	CHECK(report.resolved_count == 1);
	CHECK(report.pending_count == 0);
	CHECK(report.refused_index == 1);

	/* REFUSAL at the first encounter counts nothing resolved. */
	make_mission(&mission);
	mission.encounters[0].trigger_volume_index = 9;
	CHECK(campaign_mission_resolve(&mission, 4, &report) ==
		CAMPAIGN_MISSION_APPLY_REFUSED);
	CHECK(report.resolved_count == 0);
	CHECK(report.refused_index == 0);

	/* REFUSAL: an empty volume table refuses every encounter. */
	make_mission(&mission);
	CHECK(campaign_mission_resolve(&mission, 0, &report) ==
		CAMPAIGN_MISSION_APPLY_REFUSED);
	CHECK(report.refused_index == 0);

	/* REFUSAL: malformed records never reach volume resolution. */
	make_mission(&mission);
	mission.encounter_count = 0;
	CHECK(campaign_mission_resolve(&mission, 4, &report) ==
		CAMPAIGN_MISSION_APPLY_REFUSED);
	make_mission(&mission);
	mission.encounters[0].actor_type = _actor_marine;
	CHECK(campaign_mission_resolve(&mission, 4, &report) ==
		CAMPAIGN_MISSION_APPLY_REFUSED);

	/* REFUSAL: NULL mission refuses without touching the report. */
	report.resolved_count = 0;
	report.pending_count = 0;
	report.refused_index = -1;
	CHECK(campaign_mission_resolve(0, 4, &report) ==
		CAMPAIGN_MISSION_APPLY_REFUSED);
	CHECK(report.resolved_count == 0);
	CHECK(report.pending_count == 0);
	CHECK(report.refused_index == -1);

	/* NULL report still returns the exact status. */
	make_mission(&mission);
	CHECK(campaign_mission_resolve(&mission, 4, 0) ==
		CAMPAIGN_MISSION_APPLY_OK);
	mission.encounters[1].anchor_index = 7;
	CHECK(campaign_mission_resolve(&mission, 4, 0) ==
		CAMPAIGN_MISSION_APPLY_PENDING_ANCHOR);
	mission.encounters[1].trigger_volume_index = 4;
	CHECK(campaign_mission_resolve(&mission, 4, 0) ==
		CAMPAIGN_MISSION_APPLY_REFUSED);
	CHECK(campaign_mission_resolve(0, 4, 0) ==
		CAMPAIGN_MISSION_APPLY_REFUSED);

	printf("PASS campaign_mission_apply: %d checks\n", checks_run);
	return 0;
}
