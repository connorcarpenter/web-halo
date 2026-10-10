/*
MAIN_SCREENSHOT.H

Screenshot and framerate globals shared by MAIN.C (which defines the
store) and MAIN_FATAL.C (which reads the halt recursion lock). Moved
verbatim from MAIN.C with its packing and size assertion; layout and
behavior are unchanged.
*/

#ifndef __MAIN_SCREENSHOT_H
#define __MAIN_SCREENSHOT_H
#pragma once

/* ---------- structures */

#pragma pack(push, 1)
struct _screenshot_and_framerate_globals
{
	short count;
	byte reserved002[6];
	struct render_window windows[MAXIMUM_WINDOWS + 1];
	real framerate_samples[8];
	word framerate_flags;
	char framerate_sample_index;
	boolean framerate_active;
	char framerate_counter;
	boolean framerate_reset;
	boolean halt_recursion_lock;
};
#pragma pack(pop)

typedef char screenshot_and_framerate_globals_size_assert[
	sizeof(struct _screenshot_and_framerate_globals) == 0x38B ? 1 : -1];

/* ---------- globals */

extern struct _screenshot_and_framerate_globals global_screenshot_count;

#endif // __MAIN_SCREENSHOT_H
