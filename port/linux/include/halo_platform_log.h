#ifndef HALO_PLATFORM_LOG_H
#define HALO_PLATFORM_LOG_H

/* Shared by engine and platform units; implicit int is a different Wasm ABI. */
void platform_log(const char *format, ...) __attribute__((format(printf, 1, 2)));

#endif
