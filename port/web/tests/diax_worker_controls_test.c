/* Actual C worker lifecycle with the real Rust/Diax runtime. Fixture kind,
 * blob and serial values below are local controls, never production IDs.
 * No engine action callbacks are mocked or claimed by these byte controls. */
#include "web_diax_guest.h"
#include "diax_worker_fixture.h"
#include <stdio.h>
#include <stdlib.h>

static int checks;
#define CHECK(condition) do { \
    checks++; \
    if (!(condition)) { \
        fprintf(stderr, "worker control failed at line %d: %s\n", __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

enum { CONTROL_KIND = 123, CONTROL_BLOB_ID = 11, PAGE_BYTES = 65536, HEADER_BYTES = 84 };
static uint8_t event_byte;
static uint8_t actions[4096];
static uint8_t image[PAGE_BYTES + HEADER_BYTES];
static uint8_t restored_image[PAGE_BYTES + HEADER_BYTES];
static uint8_t error[256];
struct foreign_checks
{
    struct web_diax_worker *worker;
    int close_status;
    int dispatch_status;
    int32_t len;
};

static int bind_blob(struct web_diax_worker *worker, const uint8_t *blob,
    uint32_t len, int32_t generation, int32_t mode_kind, int32_t abi)
{
    return web_diax_worker_bind(worker, blob, len, generation, mode_kind,
        CONTROL_KIND, abi, CONTROL_BLOB_ID, NULL, 0, 100, error, sizeof(error));
}

static int bind(struct web_diax_worker *worker, int32_t generation)
{
    return bind_blob(worker, worker_control_blob, sizeof(worker_control_blob),
        generation, CONTROL_KIND, WORKER_CONTROL_ABI);
}

static int dispatch(struct web_diax_worker *worker, int32_t generation,
    uint32_t serial, int32_t *len)
{
    return web_diax_worker_dispatch(worker, generation, serial,
        &event_byte, 1, actions, sizeof(actions), len);
}

static void *foreign_thread(void *context)
{
    struct foreign_checks *result = context;
    result->close_status = web_diax_worker_close(result->worker);
    result->dispatch_status = dispatch(result->worker, 1, 101, &result->len);
    return NULL;
}

int main(void)
{
    struct web_diax_worker worker = WEB_DIAX_WORKER_INITIALIZER;
    struct halo_diax_guest *prior;
    pthread_t foreign;
    struct foreign_checks foreign_result = { .worker = &worker, .len = -77 };
    int32_t len = -77;

    CHECK(dispatch(&worker, 1, 100, &len) == -1);
    CHECK(web_diax_worker_close(&worker) == 0);
    CHECK(bind(&worker, 1) == 0);
    CHECK(worker.initial_actions_pending && worker.adapter.initial_action_len == 1);
    CHECK(dispatch(&worker, 1, 100, &len) == -1);
    CHECK(web_diax_worker_capture_guest(&worker, image, sizeof(image), &len) == -1);
    CHECK(web_diax_worker_restore_guest(&worker, 2, 100, CONTROL_BLOB_ID,
        image, sizeof(image)) == -1);
    CHECK(bind(&worker, 2) == -1);
    CHECK(web_diax_worker_take_initial_actions(&worker, NULL, 0, &len) == -1);
    CHECK(worker.initial_actions_pending && len == -77);
    CHECK(web_diax_worker_take_initial_actions(&worker, actions, sizeof(actions), &len) == 0);
    CHECK(len == 1 && actions[0] == 10 && !worker.initial_actions_pending);
    CHECK(web_diax_worker_take_initial_actions(&worker, actions, sizeof(actions), &len) == -1);
    CHECK(web_diax_worker_capture_guest(&worker, image, sizeof(image), &len) == 0);
    CHECK(len == sizeof(image) && image[HEADER_BYTES + 4] == 1);
    CHECK(dispatch(&worker, 2, 100, &len) == -1);
    CHECK(dispatch(&worker, 1, 101, &len) == -1);
    CHECK(dispatch(&worker, 1, 100, &len) == 0 && len == 1 && actions[0] == 11);
    CHECK(dispatch(&worker, 1, 100, &len) == -1);
    CHECK(pthread_create(&foreign, NULL, foreign_thread, &foreign_result) == 0);
    CHECK(pthread_join(foreign, NULL) == 0);
    CHECK(foreign_result.close_status == -1);
    CHECK(foreign_result.dispatch_status == -1);
    CHECK(foreign_result.len == -77);
    CHECK(dispatch(&worker, 1, 101, &len) == 0 && actions[0] == 12);

    prior = worker.guest;
    CHECK(bind_blob(&worker, worker_control_blob, sizeof(worker_control_blob),
        2, CONTROL_KIND + 1, WORKER_CONTROL_ABI) == -1);
    CHECK(bind_blob(&worker, (const uint8_t *)"bad", 3,
        2, CONTROL_KIND, WORKER_CONTROL_ABI) == -1);
    CHECK(bind_blob(&worker, worker_control_blob, sizeof(worker_control_blob),
        2, CONTROL_KIND, WORKER_CONTROL_ABI + 1) == -1);
    CHECK(bind_blob(&worker, worker_oversized_init_blob, sizeof(worker_oversized_init_blob),
        2, CONTROL_KIND, WORKER_CONTROL_ABI) == -1);
    CHECK(worker.guest == prior && worker.adapter.bound_generation == 1);
    CHECK(bind(&worker, 1) == -1);
    CHECK(dispatch(&worker, 1, 102, &len) == 0 && actions[0] == 13);
    CHECK(web_diax_worker_restore_guest(&worker, 2, 100, CONTROL_BLOB_ID,
        image, sizeof(image) - 1) == -1);
    CHECK(worker.adapter.bound_generation == 1);
    CHECK(web_diax_worker_restore_guest(&worker, 2, 99, CONTROL_BLOB_ID,
        image, sizeof(image)) == -1);
    image[HEADER_BYTES] ^= 1;
    CHECK(web_diax_worker_restore_guest(&worker, 2, 100, CONTROL_BLOB_ID,
        image, sizeof(image)) == -1);
    image[HEADER_BYTES] ^= 1;
    CHECK(web_diax_worker_restore_guest(&worker, 2, 100, CONTROL_BLOB_ID + 1,
        image, sizeof(image)) == -1);
    CHECK(web_diax_worker_restore_guest(&worker, 2, 100, CONTROL_BLOB_ID,
        image, sizeof(image)) == 0);
    CHECK(web_diax_worker_capture_guest(&worker, restored_image, sizeof(restored_image), &len) == 0);
    CHECK(restored_image[HEADER_BYTES + 4] == 1); /* Restore did not run __init a second time. */
    CHECK(dispatch(&worker, 1, 103, &len) == -1);
    CHECK(dispatch(&worker, 2, 100, &len) == 0 && actions[0] == 11);

    event_byte = 255;
    CHECK(dispatch(&worker, 2, 101, &len) == -1);
    event_byte = 0;
    CHECK(dispatch(&worker, 2, 101, &len) == -1);
    CHECK(worker.adapter.next_serial == 101);
    CHECK(web_diax_worker_capture_guest(&worker, image, sizeof(image), &len) == -1);
    CHECK(web_diax_worker_restore_guest(&worker, 3, 100, CONTROL_BLOB_ID,
        restored_image, sizeof(restored_image)) == 0);
    CHECK(dispatch(&worker, 3, 100, &len) == 0 && actions[0] == 11);

    prior = worker.guest;
    CHECK(bind(&worker, 4) == 0 && worker.guest != prior);
    CHECK(web_diax_worker_take_initial_actions(&worker, actions, sizeof(actions), &len) == 0);
    CHECK(actions[0] == 10);
    CHECK(bind_blob(&worker, worker_empty_init_blob, sizeof(worker_empty_init_blob),
        5, CONTROL_KIND, WORKER_CONTROL_ABI) == 0);
    CHECK(worker.initial_actions_pending && worker.adapter.initial_action_len == 0);
    CHECK(dispatch(&worker, 5, 100, &len) == -1);
    CHECK(web_diax_worker_take_initial_actions(&worker, NULL, 0, &len) == 0 && len == 0);
    CHECK(dispatch(&worker, 5, 100, &len) == 0 && actions[0] == 11);
    CHECK(web_diax_worker_capture_guest(&worker, image, sizeof(image), &len) == 0);
    CHECK(image[HEADER_BYTES + 4] == 1);
    CHECK(web_diax_worker_close(&worker) == 0 && !worker.guest);
    CHECK(worker.owner_set);
    CHECK(pthread_create(&foreign, NULL, foreign_thread, &foreign_result) == 0);
    CHECK(pthread_join(foreign, NULL) == 0);
    CHECK(foreign_result.close_status == -1);
    CHECK(bind(&worker, 6) == 0);
    CHECK(web_diax_worker_take_initial_actions(&worker, actions, sizeof(actions), &len) == 0);
    CHECK(web_diax_worker_close(&worker) == 0);
    CHECK(web_diax_worker_close(&worker) == 0);
    CHECK(dispatch(&worker, 5, 101, &len) == -1);
    printf("Diax worker controls: %d checks passed (real native C/Rust/wasmi)\n", checks);
    return 0;
}
