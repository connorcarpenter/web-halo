/*
SHELL_ENTRY.C

Program entry, separated from the reusable shell implementation in
SHELL_XBOX.C so engine controls can link the real shell providers with
their own entrypoint. The game target links this unit; its
initialization/run/disposal behavior is unchanged from when main lived
in SHELL_XBOX.C. Do not add a second entrypoint here.

symbols in this file:
	_main (0000)
*/

/* ---------- headers */

#include "cseries.h"
#include "cseries_windows.h"
#include "shell.h"
#include "shell_xbox.h"
#include "physical_memory_map.h"
#include "main.h"

#ifdef HALO_WEB
void platform_web_initialize(void);
#endif

/* ---------- prototypes */

boolean rasterizer_preinitialize__fill_you_up_with_the_devils_cock(
	void);

/* ---------- public code */

#ifdef HALO_WEB
int main(
	int argc,
	char **argv)
#else
int main(
	void)
#endif
{
#ifdef HALO_WEB
	(void)argc;
	(void)argv;
	platform_web_initialize();
#endif
	fuck_code_in_the_eye();
	rasterizer_preinitialize__fill_you_up_with_the_devils_cock();
	physical_memory_allocate();

	__try
	{
		if (shell_initialize())
		{
			main_loop();
			shell_dispose();
		}
	}
	__except (generic_exception_filter(GetExceptionCode(), GetExceptionInformation()))
	{
		halt_and_catch_fire();
	}

	return EXIT_SUCCESS;
}
