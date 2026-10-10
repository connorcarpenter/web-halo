/* Hazard Run campaign co-op session formation (Vehicle A, slice 2).
 *
 * The four-seat session is formed from the live player roster at map load:
 * create_local_players() calls campaign_coop_session_form() once the local
 * seats exist, and the result is committed for the save header paths to
 * read through campaign_coop_total_seats().
 *
 * Local-versus-remote split is deliberate. player_spawn_count stays the
 * per-machine LOCAL creation count, so every local loop bound (creation,
 * profile saves, game-time start) is untouched. The committed seat totals
 * describe the whole roster and are read only on campaign cold paths (save
 * header write, save header check, solo-save gate). When the roster is all local the
 * committed totals equal player_spawn_count exactly, so solo and two-player
 * behavior is unchanged.
 */

#include "cseries.h"
#include <stdbool.h>
#include "game/campaign_coop.h"
#include "game/players.h"
#include "main/main.h"
#include "memory/data.h"

/* Last committed session, written once per map load by
 * campaign_coop_session_commit(). Zero seats means unformed: the accessors
 * below fall back to player_spawn_count. */
static struct campaign_coop_session campaign_coop_committed_session;
static bool campaign_coop_session_committed = false;

/* Slice 3 host record, written by campaign_coop_host_configure() from the
 * browser lobby handoff. Unconfigured until the first accepted handoff;
 * reconfiguring replaces the record and clears the session_full latch. */
static bool campaign_coop_host_recorded = false;
static int16_t campaign_coop_host_recorded_episode = CAMPAIGN_COOP_NO_SEAT;
static int16_t campaign_coop_host_recorded_seats = 0;
static bool campaign_coop_full_latched = false;

bool campaign_coop_session_form(
	struct campaign_coop_session *session,
	int16_t episode_index)
{
	/* One slot past the cap so an overfull roster is observed and refused
	 * rather than silently truncated. */
	int16_t machine_indices[CAMPAIGN_COOP_MAX_SEATS + 1];
	int16_t local_player_indices[CAMPAIGN_COOP_MAX_SEATS + 1];
	long player_indices[CAMPAIGN_COOP_MAX_SEATS + 1];
	struct data_iterator iterator;
	struct player_datum *player;
	int roster_count;

	roster_count = 0;
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL &&
		roster_count <= CAMPAIGN_COOP_MAX_SEATS)
	{
		machine_indices[roster_count] = (int16_t)player->network_player_data.machine_index;
		local_player_indices[roster_count] = player->local_player_index;
		player_indices[roster_count] = iterator.datum_index;
		roster_count++;
	}

	return campaign_coop_session_form_from_roster(
		session,
		episode_index,
		machine_indices,
		local_player_indices,
		player_indices,
		roster_count);
}

void campaign_coop_session_commit(
	const struct campaign_coop_session *session)
{
	if (session == NULL)
		return;

	campaign_coop_committed_session = *session;
	campaign_coop_session_committed = true;

	return;
}

short campaign_coop_total_seats(
	void)
{
	if (!campaign_coop_session_committed)
		return player_spawn_count;

	return campaign_coop_committed_session.seat_count;
}

short campaign_coop_local_seats(
	void)
{
	short local_seats;
	short seat;

	if (!campaign_coop_session_committed)
		return player_spawn_count;

	local_seats = 0;
	for (seat = 0; seat < campaign_coop_committed_session.seat_count; seat++)
	{
		if (campaign_coop_committed_session.seats[seat].local_player_index != (int16_t)NONE)
			local_seats++;
	}

	return local_seats;
}

bool campaign_coop_host_configure(
	int16_t episode_index,
	int16_t seat_count)
{
	if (!campaign_coop_host_config_valid(episode_index, seat_count))
		return false;

	campaign_coop_host_recorded_episode = episode_index;
	campaign_coop_host_recorded_seats = seat_count;
	campaign_coop_host_recorded = true;
	campaign_coop_full_latched = false;

	return true;
}

int16_t campaign_coop_host_episode(
	void)
{
	if (!campaign_coop_host_recorded)
		return CAMPAIGN_COOP_NO_SEAT;

	return campaign_coop_host_recorded_episode;
}

short campaign_coop_host_cap(
	void)
{
	if (!campaign_coop_host_recorded)
		return 0;

	return campaign_coop_host_recorded_seats;
}

void campaign_coop_note_admit(
	bool admitted)
{
	if (admitted)
	{
		campaign_coop_full_latched = false;
	}
	else if (campaign_coop_host_recorded)
	{
		campaign_coop_full_latched = true;
	}

	return;
}

bool campaign_coop_session_full(
	void)
{
	return campaign_coop_full_latched;
}
