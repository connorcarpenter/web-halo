/* Hazard Run campaign co-op session contract (Vehicle A, slice 1).
 *
 * This header declares the four-seat campaign session shape shared by the
 * engine core (slice 2) and the web campaign-session surface. It is additive
 * only: nothing includes it yet, so the MP and two-player paths are unchanged.
 *
 * Exact-width types are deliberate: seat/roster records cross the WebRTC
 * system-link session between browser peers, so their widths must not depend
 * on the host toolchain's short/long sizes. The engine wrapper (slice 2) maps
 * these onto the port's short/long/boolean at the call boundary.
 */

#ifndef CAMPAIGN_COOP_H
#define CAMPAIGN_COOP_H

#include <stdbool.h>
#include <stdint.h>

enum
{
	/* solo campaign through a full four-seat Hazard Run roster */
	CAMPAIGN_COOP_MIN_SEATS = 1,
	CAMPAIGN_COOP_MAX_SEATS = 4,

	/* episode maps must carry at least this many campaign starting
	 * locations; spawn selection reads scenario->players at runtime */
	CAMPAIGN_COOP_REQUIRED_STARTING_LOCATIONS = 4,

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
 * machine/player roster and publishes the spawn count the campaign session
 * runs with. Returns false when the roster cannot seat a campaign session. */
bool campaign_coop_session_form(
	struct campaign_coop_session *session,
	int16_t episode_index);

#endif /* CAMPAIGN_COOP_H */
