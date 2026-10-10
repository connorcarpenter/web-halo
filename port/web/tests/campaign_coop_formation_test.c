/* Hazard Run campaign co-op formation test (Vehicle A, slices 2-3).
 *
 * Header-only standalone build: exercises the exact admission policy bytes
 * the engine runs (static inline in source/game/campaign_coop.h), with no
 * engine link. Slice 3 adds the host-handoff validation core. The stateful
 * engine glue in source/game/campaign_coop.c
 * (live roster iteration, commit, total/local accessors) is not compiled
 * here: it includes engine headers carrying a 32-bit layout assertion that
 * cannot pass under the host 64-bit gcc, so its compile belongs to the
 * native (-m32) route, not this test. Run with:
 * gcc -o /tmp/campaign_coop_formation_test \
 *   port/web/tests/campaign_coop_formation_test.c \
 *   && /tmp/campaign_coop_formation_test
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "game/campaign_coop.h"

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

int main(
	void)
{
	struct campaign_coop_session session;
	struct campaign_coop_session sentinel;
	int16_t machines[5] = { 0, 1, 2, 3, 4 };
	int16_t locals[5] = { 0, 0, 0, 0, 0 };
	long indices[5] = { 10, 11, 12, 13, 14 };

	/* Helpers: solo and two-player defaults pass through unchanged. */
	CHECK(campaign_coop_clamp_seat_count(0) == 1);
	CHECK(campaign_coop_clamp_seat_count(1) == 1);
	CHECK(campaign_coop_clamp_seat_count(2) == 2);
	CHECK(campaign_coop_clamp_seat_count(4) == 4);
	CHECK(campaign_coop_clamp_seat_count(5) == 4);
	CHECK(campaign_coop_seat_count_valid(0) == false);
	CHECK(campaign_coop_seat_count_valid(1) == true);
	CHECK(campaign_coop_seat_count_valid(4) == true);
	CHECK(campaign_coop_seat_count_valid(5) == false);

	/* Solo roster forms with its episode. */
	CHECK(campaign_coop_session_form_from_roster(&session, 3, machines, locals, indices, 1) == true);
	CHECK(session.episode_index == 3);
	CHECK(session.seat_count == 1);
	CHECK(session.seats[0].machine_index == 0);
	CHECK(session.seats[0].local_player_index == 0);
	CHECK(session.seats[0].player_index == 10);

	/* Full four-seat distributed roster forms. */
	CHECK(campaign_coop_session_form_from_roster(&session, 0, machines, locals, indices, 4) == true);
	CHECK(session.seat_count == 4);
	CHECK(session.seats[3].machine_index == 3);
	CHECK(session.seats[3].player_index == 13);

	/* Remote seats carry CAMPAIGN_COOP_NO_SEAT as their local slot. */
	locals[1] = CAMPAIGN_COOP_NO_SEAT;
	CHECK(campaign_coop_session_form_from_roster(&session, 1, machines, locals, indices, 2) == true);
	CHECK(session.seat_count == 2);
	CHECK(session.seats[1].local_player_index == CAMPAIGN_COOP_NO_SEAT);
	CHECK(session.seats[1].player_index == 11);
	locals[1] = 0;

	/* Unbound episode (engine map-load hook) still forms a committable roster. */
	CHECK(campaign_coop_session_form_from_roster(&session, CAMPAIGN_COOP_NO_SEAT, machines, locals, indices, 2) == true);
	CHECK(session.episode_index == CAMPAIGN_COOP_NO_SEAT);
	CHECK(session.seat_count == 2);

	/* Slice 3 host handoff validation: eight zero-based episodes, 1..4
	 * seats. Anything else is refused and the prior record stands. */
	CHECK(CAMPAIGN_COOP_EPISODE_COUNT == 8);
	CHECK(campaign_coop_host_config_valid(0, 1) == true);
	CHECK(campaign_coop_host_config_valid(7, 4) == true);
	CHECK(campaign_coop_host_config_valid(3, 2) == true);
	CHECK(campaign_coop_host_config_valid(CAMPAIGN_COOP_NO_SEAT, 2) == false);
	CHECK(campaign_coop_host_config_valid(8, 2) == false);
	CHECK(campaign_coop_host_config_valid(0, 0) == false);
	CHECK(campaign_coop_host_config_valid(0, 5) == false);
	CHECK(campaign_coop_host_config_valid(-2, 9) == false);

	/* NULL session is refused, never dereferenced. */
	CHECK(campaign_coop_session_form_from_roster(0, 0, machines, locals, indices, 2) == false);

	/* Empty and overfull rosters are refused and leave the session alone:
	 * the fifth seat in a four-seat session is the case this guard exists
	 * for, and it must never clamp-masquerade as a four-seat session. */
	memset(&sentinel, 0x5A, sizeof(sentinel));
	session = sentinel;
	CHECK(campaign_coop_session_form_from_roster(&session, 0, machines, locals, indices, 0) == false);
	CHECK(memcmp(&session, &sentinel, sizeof(session)) == 0);
	CHECK(campaign_coop_session_form_from_roster(&session, 0, machines, locals, indices, 5) == false);
	CHECK(memcmp(&session, &sentinel, sizeof(session)) == 0);

	printf("PASS campaign_coop_formation_test: %d checks\n", checks_run);
	return 0;
}
