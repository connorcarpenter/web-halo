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
#include "game/campaign_coop_adapter.h"
#include "game/campaign_mission_apply.h"
#include "game/campaign_mission_associate.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"

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
static struct scenario *stub_scenario;
static int stub_tag_reads;

struct scenario *global_scenario_try_and_get(void)
{
	return stub_scenario;
}

void *tag_block_get_element_with_size(const struct tag_block *block,
	long index, long element_size)
{
	stub_tag_reads++;
	if (!block || index < 0 || index >= block->count || !block->address)
		return 0;
	return (unsigned char *)block->address + index * element_size;
}

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

/* ---------- adapter guest-slot stubs: guest behavior only ---------- */

static int32_t stub_adapter_abi = 7;
static int stub_adapter_init_calls;
static int stub_adapter_init_fail;
static int stub_adapter_event_fail;
static int stub_adapter_restore_fail;
static int32_t stub_adapter_restored_length;

static int32_t stub_adapter_abi_version(
	void)
{
	return stub_adapter_abi;
}

static int stub_adapter_init(
	const uint8_t *params_in,
	int32_t params_len)
{
	(void)params_in;
	(void)params_len;
	stub_adapter_init_calls++;
	return stub_adapter_init_fail ? 1 : 0;
}

static int stub_adapter_handle(
	const uint8_t *event_in,
	int32_t event_len,
	uint8_t *action_out,
	int32_t action_capacity)
{
	int32_t i;

	if (stub_adapter_event_fail)
		return -1;
	if (event_len > action_capacity)
		return -1;
	for (i = 0; i < event_len; i++)
		action_out[i] = event_in[i];
	return event_len;
}

static int stub_adapter_capture(
	uint8_t *image_out,
	int32_t image_capacity)
{
	if (image_capacity < 4)
		return -1;
	image_out[0] = 1;
	image_out[1] = 2;
	image_out[2] = 3;
	image_out[3] = 4;
	return 4;
}

static int stub_adapter_restore(
	const uint8_t *image_in,
	int32_t image_len)
{
	(void)image_in;
	stub_adapter_restored_length = image_len;
	return stub_adapter_restore_fail ? 1 : 0;
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
		mission.encounter_count = -1;
		CHECK(campaign_mission_save("hazard_e1", &mission) == false);
		mission.encounter_count = 1;
		CHECK(campaign_mission_load("hazard_e1", &reloaded) == true);
		CHECK(reloaded.encounter_count == 1);

		/* Work-only E1: zero hostiles saves/loads with no dummy encounter. */
		mission.encounter_count = 0;
		CHECK(campaign_mission_save("hazard_e1", &mission) == true);
		CHECK(campaign_mission_load("hazard_e1", &reloaded) == true);
		CHECK(reloaded.encounter_count == 0);

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

	/* Real production author/save/load/apply/associate path on a selected
	 * scenario with NO volumes/AI rows. Only backing data is controlled. */
	{
		char name[CAMPAIGN_MISSION_NAME_CHARACTERS] = "Routine Call";
		struct campaign_mission loaded;
		static struct scenario empty_scenario;
		struct campaign_mission_apply_report report;
		int recorded = -1;
		int16_t bound = -1;
		int16_t count = -1;
		CHECK(campaign_mission_author_commit(0, name,
			0, 0, 0, 0, 0, &recorded) == CAMPAIGN_MISSION_AUTHOR_SAVED);
		CHECK(recorded == 0);
		CHECK(campaign_mission_load_episode(0, &loaded));
		CHECK(loaded.encounter_count == 0);
		CHECK(loaded.encounters[0].actor_count == 0);
		CHECK(campaign_mission_activate_bound(0, &report, &bound) ==
			CAMPAIGN_MISSION_APPLY_REFUSED);
		stub_scenario = &empty_scenario;
		CHECK(campaign_mission_activate_bound(0, &report, &bound) == CAMPAIGN_MISSION_APPLY_OK);
		CHECK(bound == 0);
		CHECK(report.resolved_count == 0 && report.pending_count == 0);
		CHECK(campaign_mission_activated()->encounter_count == 0);
		CHECK(campaign_mission_bound_table(&count) != 0 && count == 0);
		CHECK(stub_tag_reads == 0);
		/* A malformed replacement never overwrites the work-only save. */
		CHECK(campaign_mission_author_commit(0, name,
			0, 0, 0, 0, -1, &recorded) == CAMPAIGN_MISSION_AUTHOR_BAD_ENCOUNTER);
		CHECK(campaign_mission_load_episode(0, &loaded) && loaded.encounter_count == 0);
		/* A valid hostile E2 record still refuses an out-of-range map volume
		 * and preserves the prior activated work-only mission. */
		loaded.episode_index = 1;
		loaded.encounter_count = 1;
		loaded.encounters[0].trigger_volume_index = 3;
		loaded.encounters[0].actor_type = _actor_grunt;
		loaded.encounters[0].actor_count = 1;
		loaded.encounters[0].anchor_index = CAMPAIGN_MISSION_NO_ANCHOR;
		CHECK(campaign_mission_save("test_hostile", &loaded));
		CHECK(campaign_mission_activate_bound(1, &report, &bound) == CAMPAIGN_MISSION_APPLY_REFUSED);
		CHECK(report.refused_index == 0);
		CHECK(campaign_mission_activated()->encounter_count == 0);
		stub_scenario = 0;
	}

	/* ADAPTER controls (cooperative guest slice): serialized bind,
	 * fenced dispatch, capture, and generation-advancing restore with
	 * no post-restore __init. Schema-owned values (kind, ABI, blob)
	 * arrive as bind parameters; the stubs above stand in for the
	 * runtime wrapper's guest slots. */
	{
		struct campaign_coop_adapter adapter;
		struct campaign_coop_adapter adapter2;
		struct campaign_coop_adapter_slots slots;
		uint8_t event_in[8] = { 9, 8, 7, 6, 5, 4, 3, 2 };
		uint8_t action_out[16];
		uint8_t image[32];
		int32_t produced = -1;

		slots.guest_abi_version = stub_adapter_abi_version;
		slots.guest_init = stub_adapter_init;
		slots.guest_handle_event = stub_adapter_handle;
		slots.guest_capture_image = stub_adapter_capture;
		slots.guest_restore_image = stub_adapter_restore;

		/* Unbound adapters refuse every guest-touching call. */
		adapter.bound_generation = CAMPAIGN_COOP_ADAPTER_NO_GENERATION;
		CHECK(campaign_coop_adapter_bound(&adapter) == false);
		CHECK(campaign_coop_adapter_dispatch(&adapter, 0, 100,
			event_in, 8, action_out, 16, &produced) ==
			CAMPAIGN_COOP_ADAPTER_REFUSED);
		CHECK(campaign_coop_adapter_capture(&adapter, image, 32,
			&produced) == CAMPAIGN_COOP_ADAPTER_REFUSED);
		CHECK(campaign_coop_adapter_restore(&adapter, 1, 200, 11,
			image, 4) == CAMPAIGN_COOP_ADAPTER_REFUSED);

		/* Incomplete slots bind nothing. */
		slots.guest_handle_event = 0;
		CHECK(campaign_coop_adapter_bind(&adapter, 0, 3, 3, 7, 11,
			&slots, event_in, 8, 100) ==
			CAMPAIGN_COOP_ADAPTER_REFUSED);
		slots.guest_handle_event = stub_adapter_handle;
		CHECK(campaign_coop_adapter_bound(&adapter) == false);

		/* NULL adapter, bad generation, and kind mismatch refuse. */
		CHECK(campaign_coop_adapter_bind(0, 0, 3, 3, 7, 11,
			&slots, event_in, 8, 100) ==
			CAMPAIGN_COOP_ADAPTER_REFUSED);
		CHECK(campaign_coop_adapter_bind(&adapter, -1, 3, 3, 7, 11,
			&slots, event_in, 8, 100) ==
			CAMPAIGN_COOP_ADAPTER_REFUSED);
		CHECK(campaign_coop_adapter_bind(&adapter, 0, 4, 3, 7, 11,
			&slots, event_in, 8, 100) ==
			CAMPAIGN_COOP_ADAPTER_REFUSED);

		/* ABI mismatch refuses before __init runs. */
		stub_adapter_abi = 8;
		CHECK(campaign_coop_adapter_bind(&adapter, 0, 3, 3, 7, 11,
			&slots, event_in, 8, 100) ==
			CAMPAIGN_COOP_ADAPTER_REFUSED);
		CHECK(stub_adapter_init_calls == 0);
		stub_adapter_abi = 7;

		/* A failing __init refuses and binds nothing. */
		stub_adapter_init_fail = 1;
		CHECK(campaign_coop_adapter_bind(&adapter, 0, 3, 3, 7, 11,
			&slots, event_in, 8, 100) ==
			CAMPAIGN_COOP_ADAPTER_REFUSED);
		CHECK(stub_adapter_init_calls == 1);
		CHECK(campaign_coop_adapter_bound(&adapter) == false);
		stub_adapter_init_fail = 0;

		/* Good bind: kind gate, handshake, exactly one __init. */
		CHECK(campaign_coop_adapter_bind(&adapter, 0, 3, 3, 7, 11,
			&slots, event_in, 8, 100) ==
			CAMPAIGN_COOP_ADAPTER_OK);
		CHECK(stub_adapter_init_calls == 2);
		CHECK(campaign_coop_adapter_bound(&adapter) == true);

		/* Empty init params bind a second adapter cleanly. */
		CHECK(campaign_coop_adapter_bind(&adapter2, 0, 3, 3, 7, 11,
			&slots, 0, 0, 1000) == CAMPAIGN_COOP_ADAPTER_OK);
		CHECK(stub_adapter_init_calls == 3);

		/* Dispatch fence: wrong generation or serial refuses
		 * untouched; the fenced serial dispatches exactly once. */
		CHECK(campaign_coop_adapter_dispatch(&adapter, 1, 100,
			event_in, 8, action_out, 16, &produced) ==
			CAMPAIGN_COOP_ADAPTER_REFUSED);
		CHECK(campaign_coop_adapter_dispatch(&adapter, 0, 101,
			event_in, 8, action_out, 16, &produced) ==
			CAMPAIGN_COOP_ADAPTER_REFUSED);
		CHECK(campaign_coop_adapter_dispatch(&adapter, 0, 100,
			event_in, 8, action_out, 16, &produced) ==
			CAMPAIGN_COOP_ADAPTER_OK);
		CHECK(produced == 8);
		CHECK(action_out[0] == 9 && action_out[7] == 2);
		CHECK(campaign_coop_adapter_dispatch(&adapter, 0, 100,
			event_in, 8, action_out, 16, &produced) ==
			CAMPAIGN_COOP_ADAPTER_REFUSED);

		/* A guest refusal preserves the fence for retry. */
		stub_adapter_event_fail = 1;
		CHECK(campaign_coop_adapter_dispatch(&adapter, 0, 101,
			event_in, 8, action_out, 16, &produced) ==
			CAMPAIGN_COOP_ADAPTER_REFUSED);
		stub_adapter_event_fail = 0;
		CHECK(campaign_coop_adapter_dispatch(&adapter, 0, 101,
			event_in, 8, action_out, 16, &produced) ==
			CAMPAIGN_COOP_ADAPTER_OK);
		CHECK(produced == 8);

		/* Capture carries the guest image. */
		CHECK(campaign_coop_adapter_capture(&adapter, image, 32,
			&produced) == CAMPAIGN_COOP_ADAPTER_OK);
		CHECK(produced == 4);
		CHECK(image[0] == 1 && image[3] == 4);

		/* Restore refusals keep the prior binding intact. */
		CHECK(campaign_coop_adapter_restore(&adapter, 0, 200, 11,
			image, 4) == CAMPAIGN_COOP_ADAPTER_REFUSED);
		CHECK(campaign_coop_adapter_restore(&adapter, 1, 200, 12,
			image, 4) == CAMPAIGN_COOP_ADAPTER_REFUSED);
		stub_adapter_restore_fail = 1;
		CHECK(campaign_coop_adapter_restore(&adapter, 1, 200, 11,
			image, 4) == CAMPAIGN_COOP_ADAPTER_REFUSED);
		stub_adapter_restore_fail = 0;
		CHECK(campaign_coop_adapter_dispatch(&adapter, 0, 102,
			event_in, 8, action_out, 16, &produced) ==
			CAMPAIGN_COOP_ADAPTER_OK);

		/* Good restore advances the generation, resets the fence,
		 * and never re-runs __init; the old generation is dead. */
		CHECK(campaign_coop_adapter_restore(&adapter, 1, 200, 11,
			image, 4) == CAMPAIGN_COOP_ADAPTER_OK);
		CHECK(stub_adapter_init_calls == 3);
		CHECK(campaign_coop_adapter_dispatch(&adapter, 0, 102,
			event_in, 8, action_out, 16, &produced) ==
			CAMPAIGN_COOP_ADAPTER_REFUSED);
		CHECK(campaign_coop_adapter_dispatch(&adapter, 1, 200,
			event_in, 8, action_out, 16, &produced) ==
			CAMPAIGN_COOP_ADAPTER_OK);
		CHECK(produced == 8);

		/* Complete Wasm memory images exceed the event-record ceiling.
		 * Carry a whole page to the runtime slot, preserving the init
		 * count and invalidating the preceding generation's traffic. */
		{
			static uint8_t page_image[65536];
			CHECK(campaign_coop_adapter_restore(&adapter, 2, 300, 11,
				page_image, sizeof(page_image)) == CAMPAIGN_COOP_ADAPTER_OK);
			CHECK(stub_adapter_restored_length == sizeof(page_image));
			CHECK(stub_adapter_init_calls == 3);
			CHECK(campaign_coop_adapter_dispatch(&adapter, 1, 201,
				event_in, 8, action_out, 16, &produced) ==
				CAMPAIGN_COOP_ADAPTER_REFUSED);
			CHECK(campaign_coop_adapter_dispatch(&adapter, 2, 300,
				event_in, 8, action_out, 16, &produced) ==
				CAMPAIGN_COOP_ADAPTER_OK);
		}
	}

	printf("PASS campaign_coop_native_controls: %d checks\n", checks_run);
	return 0;
}
