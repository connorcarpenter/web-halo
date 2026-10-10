/* Hazard Run campaign co-op session contract (Vehicle A, slice 1).
 *
 * This header declares the four-seat campaign session shape shared by the
 * engine core (slice 2) and the web campaign-session surface. The engine
 * hook (create_local_players) and the campaign cold-path readers (save
 * header, solo map select) include it; the MP and two-player paths read
 * unchanged values through it.
 *
 * Exact-width types are deliberate: seat/roster records cross the WebRTC
 * system-link session between browser peers, so their widths must not depend
 * on the host toolchain's short/long sizes. The engine wrapper (slice 2) maps
 * these onto the port's short/long/boolean at the call boundary.
 */

#ifndef CAMPAIGN_COOP_H
#define CAMPAIGN_COOP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum
{
	/* solo campaign through a full four-seat Hazard Run roster */
	CAMPAIGN_COOP_MIN_SEATS = 1,
	CAMPAIGN_COOP_MAX_SEATS = 4,

	/* episode maps must carry at least this many campaign starting
	 * locations; spawn selection reads scenario->players at runtime */
	CAMPAIGN_COOP_REQUIRED_STARTING_LOCATIONS = 4,

	/* eight authored Hazard Run episodes, zero-based on the wire and in
	 * the recorded host handoff (episode 1 == 0) */
	CAMPAIGN_COOP_EPISODE_COUNT = 8,

	/* matches the engine NONE convention for an unseated slot */
	CAMPAIGN_COOP_NO_SEAT = -1,
};

struct campaign_coop_seat
{
	/* index into the system-link machine table */
	int16_t machine_index;
	/* local slot on that machine (0 .. MAXIMUM_LOCAL_PLAYERS-1) */
	int16_t local_player_index;
	/* roster-wide player index once admitted, else CAMPAIGN_COOP_NO_SEAT */
	int32_t player_index;
};

struct campaign_coop_session
{
	/* zero-based episode within the campaign (episode 1 == 0) */
	int16_t episode_index;
	/* admitted seats, CAMPAIGN_COOP_MIN_SEATS .. CAMPAIGN_COOP_MAX_SEATS */
	int16_t seat_count;
	struct campaign_coop_seat seats[CAMPAIGN_COOP_MAX_SEATS];
};

static inline bool campaign_coop_seat_count_valid(
	int16_t seat_count)
{
	return seat_count >= CAMPAIGN_COOP_MIN_SEATS &&
		seat_count <= CAMPAIGN_COOP_MAX_SEATS;
}

/* Clamp a requested roster size into the contract range. The solo and
 * two-player defaults pass through unchanged. */
static inline int16_t campaign_coop_clamp_seat_count(
	int16_t requested)
{
	if (requested < CAMPAIGN_COOP_MIN_SEATS)
		return CAMPAIGN_COOP_MIN_SEATS;
	if (requested > CAMPAIGN_COOP_MAX_SEATS)
		return CAMPAIGN_COOP_MAX_SEATS;
	return requested;
}

/* Slice 2 engine implementation. Forms seats from the admitted
 * machine/player roster. Returns false when the roster cannot seat a campaign
 * session (fewer than CAMPAIGN_COOP_MIN_SEATS or more than
 * CAMPAIGN_COOP_MAX_SEATS seats); the session is left untouched on refusal.
 * The episode index may be CAMPAIGN_COOP_NO_SEAT when formation runs before
 * the content layer binds the episode (engine map-load hook); the roster is
 * still formed and committable. */
bool campaign_coop_session_form(
	struct campaign_coop_session *session,
	int16_t episode_index);

/* Pure roster core behind session_form, over caller-supplied parallel arrays
 * (machine index per seat, local slot per seat with CAMPAIGN_COOP_NO_SEAT
 * for remote seats, roster-wide player index per seat). Strict admission: a
 * roster that cannot seat a campaign session is refused, never clamped, and
 * the session is left untouched. Static inline so the policy is exactly the
 * same bytes in the engine and in the standalone formation test; no engine
 * globals are read. */
static inline bool campaign_coop_session_form_from_roster(
	struct campaign_coop_session *session,
	int16_t episode_index,
	const int16_t machine_indices[],
	const int16_t local_player_indices[],
	const long player_indices[],
	int roster_count)
{
	int seat;

	if (session == NULL)
		return false;
	if (!campaign_coop_seat_count_valid((int16_t)roster_count))
		return false;

	session->episode_index = episode_index;
	session->seat_count = (int16_t)roster_count;
	for (seat = 0; seat < roster_count; seat++)
	{
		session->seats[seat].machine_index = machine_indices[seat];
		session->seats[seat].local_player_index = local_player_indices[seat];
		session->seats[seat].player_index = (int32_t)player_indices[seat];
	}

	return true;
}

/* Commits a formed session for the save header paths to read. Called once
 * per map load after session_form; overwritten on the next map load. */
void campaign_coop_session_commit(
	const struct campaign_coop_session *session);

/* Whole-roster seat count from the last commit. Before the first commit
 * (main menu, multiplayer maps) this equals player_spawn_count, so every
 * pre-existing reader keeps its value. Read only on campaign cold paths:
 * save header write, save header check, solo-save gate. The solo map-select
 * switch stays on the local player_spawn_count by design (it reads local
 * profiles, and its default arm halts on counts above 2). */
short campaign_coop_total_seats(
	void);

/* Committed seats local to this machine (local_player_index != NONE).
 * Same pre-commit fallback as campaign_coop_total_seats. */
short campaign_coop_local_seats(
	void);

/* Pure host-handoff core behind campaign_coop_host_configure, over the two
 * recorded values. Static inline so the validation is exactly the same
 * bytes in the engine and in the standalone formation test. */
static inline bool campaign_coop_host_config_valid(
	int16_t episode_index,
	int16_t seat_count)
{
	return episode_index >= 0 &&
		episode_index < CAMPAIGN_COOP_EPISODE_COUNT &&
		campaign_coop_seat_count_valid(seat_count);
}

/* Slice 3 host bring-up. Records the browser lobby's (episode, seats)
 * handoff for this machine's campaign session: binds the episode the next
 * map-load form carries, and arms the admit cap the server path reads
 * through campaign_coop_host_cap(). Strict: an out-of-range handoff is
 * refused and the prior record stands. Returns false on refusal. */
bool campaign_coop_host_configure(
	int16_t episode_index,
	int16_t seat_count);

/* Recorded episode for the next form, or CAMPAIGN_COOP_NO_SEAT when no
 * host handoff is recorded. */
int16_t campaign_coop_host_episode(
	void);

/* Armed admit cap for the server path, or 0 when no host handoff is
 * recorded (multiplayer and unconfigured campaign host games keep their
 * own maximum_player_count). */
short campaign_coop_host_cap(
	void);

/* Notes a server admit-path outcome for the session_full wire: a refusal
 * latches session_full only while a host cap is armed (a full multiplayer
 * game is not a full campaign session); any admission clears the latch. */
void campaign_coop_note_admit(
	bool admitted);

/* Latched campaign session_full: true once a join was refused at the
 * armed host cap, until the next admission or host reconfigure. False
 * before any host handoff. */
bool campaign_coop_session_full(
	void);

#endif /* CAMPAIGN_COOP_H */
