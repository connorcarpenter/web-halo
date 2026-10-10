/* Hazard Run campaign formation native controls (Vehicle A, slices 2-3).
 *
 * NATIVE-TOOLCHAIN ONLY. Do not build this file with the host gcc: it
 * includes real engine headers (game/players.h, memory/data.h) that carry
 * a 32-bit layout assertion, so it compiles only under the native (-m32)
 * DAG with the port prefix and generated semantics headers. It is driven
 * by the native build tooling, never by hand.
 *
 * Unlike port/web/tests/campaign_coop_formation_test.c (header policy
 * only), this file compiles the UNMODIFIED source/game/campaign_coop.c and
 * drives its real live path: the data-iterator roster loop in
 * campaign_coop_session_form(), the commit store, the total/local
 * accessors, and (slice 3) the host record with the session_full wire.
 * Only the backing store is stubbed at link level — the fake
 * player_data served through stub data_iterator_new/next, and the
 * player_spawn_count fallback value. Every policy byte under test ships
 * in the game.
 *
 * Native DAG shape (tooling owner wires this; shown for reference):
 * compile source/game/campaign_coop.c + source/game/campaign_mission.c
 * + this file with the game-lib cflags, link with the stub definitions
 * below satisfying the engine externals, run, expect exit 0 and the
 * PASS line.
 */

#include <stdio.h>

/* The game-lib build puts port/linux/include/stdio.h first, which redirects
 * printf to halo_linux_printf (implemented in msvc_crt.c, outside the
 * two-object controls link). Test reporting is not game behavior, so use
 * the host libc printer instead; production bytes are unaffected. */
#undef printf

#include "cseries.h"
#include "memory/data.h"
#include "game/players.h"
#include "game/campaign_coop.h"
#include "game/campaign_mission.h"

/* ---------- link-level stubs: backing store only ---------- */

short player_spawn_count = 1;
struct data_array *player_data = 0;

static struct
{
	char machine_index;
	short local_player_index;
	long player_index;
} fake_roster[CAMPAIGN_COOP_MAX_SEATS + 1];
static int fake_roster_count;
static int fake_roster_pos;

void data_iterator_new(
	struct data_iterator *iterator,
	struct data_array *data)
{
	(void)data;
	fake_roster_pos = 0;
	iterator->datum_index = -1;
}

void *data_iterator_next(
	struct data_iterator *iterator)
{
	static struct player_datum backing[CAMPAIGN_COOP_MAX_SEATS + 1];

	if (fake_roster_pos >= fake_roster_count)
		return 0;
	backing[fake_roster_pos].network_player_data.machine_index =
		fake_roster[fake_roster_pos].machine_index;
	backing[fake_roster_pos].local_player_index =
		fake_roster[fake_roster_pos].local_player_index;
	iterator->datum_index = fake_roster[fake_roster_pos].player_index;
	fake_roster_pos++;
	return &backing[fake_roster_pos - 1];
}

/* Datastore stub: one flat field store, so save/load runs its real
 * round-trip bytes with no filesystem. */
static unsigned char stub_datastore[sizeof(struct campaign_mission)];
static long stub_datastore_length = -1;

boolean datastore_write(
	const char *file_name,
	const char *field_name,
	long length,
	const void *data)
{
	const unsigned char *bytes = (const unsigned char *)data;
	long i;

	(void)file_name;
	(void)field_name;
	if (!data || length != (long)sizeof(stub_datastore))
		return FALSE;
	for (i = 0; i < length; i++)
		stub_datastore[i] = bytes[i];
	stub_datastore_length = length;
	return TRUE;
}

boolean datastore_read(
	const char *file_name,
	const char *field_name,
	long length,
	void *data)
{
	unsigned char *bytes = (unsigned char *)data;
	long i;

	(void)file_name;
	(void)field_name;
	if (!data || stub_datastore_length != length)
		return FALSE;
	for (i = 0; i < length; i++)
		bytes[i] = stub_datastore[i];
	return TRUE;
}

/* ---------- controls ---------- */

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

static void set_fake_roster(
	int count)
{
	int seat;

	fake_roster_count = count;
	fake_roster_pos = 0;
	for (seat = 0; seat < count; seat++)
	{
		fake_roster[seat].machine_index = (char)seat;
		fake_roster[seat].local_player_index = 0;
		fake_roster[seat].player_index = 100 + seat;
	}
}

int main(
	void)
{
	struct campaign_coop_session session;

	/* ENTRY guard: before any commit the readers keep the old values. */
	player_spawn_count = 2;
	CHECK(campaign_coop_total_seats() == 2);
	CHECK(campaign_coop_local_seats() == 2);

	/* ROSTER control: two local seats form through the real iterator loop. */
	set_fake_roster(2);
	CHECK(campaign_coop_session_form(&session, CAMPAIGN_COOP_NO_SEAT) == TRUE);
	CHECK(session.seat_count == 2);
	CHECK(session.episode_index == CAMPAIGN_COOP_NO_SEAT);
	CHECK(session.seats[0].machine_index == 0);
	CHECK(session.seats[0].local_player_index == 0);
	CHECK(session.seats[0].player_index == 100);
	CHECK(session.seats[1].machine_index == 1);
	CHECK(session.seats[1].player_index == 101);
	campaign_coop_session_commit(&session);

	/* SAVE-reader controls: the committed totals are what the save header
	 * write, check, and solo-gate read. */
	CHECK(campaign_coop_total_seats() == 2);
	CHECK(campaign_coop_local_seats() == 2);

	/* ROSTER control with a remote seat: total counts it, local does not. */
	set_fake_roster(2);
	fake_roster[1].local_player_index = (short)NONE;
	CHECK(campaign_coop_session_form(&session, 1) == TRUE);
	campaign_coop_session_commit(&session);
	CHECK(campaign_coop_total_seats() == 2);
	CHECK(campaign_coop_local_seats() == 1);

	/* REFUSAL control: five admitted seats cannot form; the prior commit
	 * still reads back, so a refused form commits nothing. */
	set_fake_roster(5);
	CHECK(campaign_coop_session_form(&session, 0) == FALSE);
	CHECK(campaign_coop_total_seats() == 2);
	CHECK(campaign_coop_local_seats() == 1);

	/* NULL safety on the commit/form boundary. */
	campaign_coop_session_commit(0);
	CHECK(campaign_coop_session_form(0, 0) == FALSE);

	/* HOST controls (slice 3): the unconfigured record reads empty, and a
	 * refusal with no armed cap leaves the session_full wire alone (a full
	 * multiplayer game is not a full campaign session). */
	CHECK(campaign_coop_host_episode() == CAMPAIGN_COOP_NO_SEAT);
	CHECK(campaign_coop_host_cap() == 0);
	CHECK(campaign_coop_session_full() == FALSE);
	campaign_coop_note_admit(FALSE);
	CHECK(campaign_coop_session_full() == FALSE);

	/* Refused handoffs record nothing; the prior (empty) record stands. */
	CHECK(campaign_coop_host_configure(8, 2) == FALSE);
	CHECK(campaign_coop_host_configure(0, 5) == FALSE);
	CHECK(campaign_coop_host_configure(CAMPAIGN_COOP_NO_SEAT, 0) == FALSE);
	CHECK(campaign_coop_host_episode() == CAMPAIGN_COOP_NO_SEAT);
	CHECK(campaign_coop_host_cap() == 0);

	/* Accepted handoff binds the episode and arms the admit cap. */
	CHECK(campaign_coop_host_configure(2, 3) == TRUE);
	CHECK(campaign_coop_host_episode() == 2);
	CHECK(campaign_coop_host_cap() == 3);
	CHECK(campaign_coop_session_full() == FALSE);

	/* Wire: an armed-cap refusal latches full; an admission clears it. */
	campaign_coop_note_admit(FALSE);
	CHECK(campaign_coop_session_full() == TRUE);
	campaign_coop_note_admit(TRUE);
	CHECK(campaign_coop_session_full() == FALSE);

	/* Reconfigure replaces the record and clears the latch. */
	campaign_coop_note_admit(FALSE);
	CHECK(campaign_coop_session_full() == TRUE);
	CHECK(campaign_coop_host_configure(0, 1) == TRUE);
	CHECK(campaign_coop_host_episode() == 0);
	CHECK(campaign_coop_host_cap() == 1);
	CHECK(campaign_coop_session_full() == FALSE);

	/* MISSION controls (authoring slice 1): save validates first, load
	 * validates what the store returns, and the round trip is exact. */
	{
		struct campaign_mission mission;
		struct campaign_mission reloaded;
		unsigned int i;

		for (i = 0; i < sizeof(mission.name); i++)
			mission.name[i] = '\0';
		mission.version = CAMPAIGN_MISSION_VERSION;
		mission.episode_index = 0;
		mission.name[0] = 'E';
		mission.name[1] = '1';
		mission.encounter_count = 1;
		mission.encounters[0].trigger_volume_index = 0;
		mission.encounters[0].actor_type = _actor_grunt;
		mission.encounters[0].actor_count = 4;
		mission.encounters[0].anchor_index = CAMPAIGN_MISSION_NO_ANCHOR;

		CHECK(campaign_mission_save("hazard_e1", &mission) == true);
		for (i = 0; i < sizeof(reloaded.name); i++)
			reloaded.name[i] = (char)0xA5;
		reloaded.version = 0;
		reloaded.episode_index = -1;
		reloaded.encounter_count = 0;
		CHECK(campaign_mission_load("hazard_e1", &reloaded) == true);
		CHECK(reloaded.version == CAMPAIGN_MISSION_VERSION);
		CHECK(reloaded.episode_index == 0);
		CHECK(reloaded.name[0] == 'E' && reloaded.name[1] == '1');
		CHECK(reloaded.encounter_count == 1);
		CHECK(reloaded.encounters[0].trigger_volume_index == 0);
		CHECK(reloaded.encounters[0].actor_type == _actor_grunt);
		CHECK(reloaded.encounters[0].actor_count == 4);
		CHECK(reloaded.encounters[0].anchor_index == CAMPAIGN_MISSION_NO_ANCHOR);

		/* Invalid records never reach the store; the prior bytes stand. */
		mission.encounter_count = 0;
		CHECK(campaign_mission_save("hazard_e1", &mission) == false);
		mission.encounter_count = 1;
		CHECK(campaign_mission_load("hazard_e1", &reloaded) == true);
		CHECK(reloaded.encounter_count == 1);

		/* Corrupt stores fail closed and leave the out-param alone. */
		stub_datastore[0] = 0;
		stub_datastore[1] = 0;
		reloaded.version = CAMPAIGN_MISSION_VERSION;
		reloaded.episode_index = 3;
		CHECK(campaign_mission_load("hazard_e1", &reloaded) == false);
		CHECK(reloaded.episode_index == 3);

		/* Empty store and NULLs fail without touching the out-param. */
		stub_datastore_length = -1;
		CHECK(campaign_mission_load("hazard_e1", &reloaded) == false);
		CHECK(campaign_mission_save(0, &mission) == false);
		CHECK(campaign_mission_save("hazard_e1", 0) == false);
		CHECK(campaign_mission_load(0, &reloaded) == false);
		CHECK(campaign_mission_load("hazard_e1", 0) == false);
	}

	printf("PASS campaign_coop_native_controls: %d checks\n", checks_run);
	return 0;
}
