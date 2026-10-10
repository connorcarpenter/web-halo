use super::*;
use modding_abi::ABI_VERSION_PACKED;
use modding_runtime::WASM_PAGE_BYTES;
#[path = "control_fixture.rs"]
mod fixture;
use fixture::blob;

fn consume(guest: &mut Guest) -> u8 {
    let mut output = [0];
    assert_eq!(guest.take_output(&mut output), 1);
    output[0]
}

#[test]
fn init_actions_are_retained_until_consumed() {
    let mut guest = Guest::load(&blob()).unwrap();
    assert_eq!(guest.abi, ABI_VERSION_PACKED);
    assert_eq!(guest.call(false, &[]), -1);
    assert_eq!(guest.call(true, &[]), 1);
    assert_eq!(guest.take_output(&mut []), -1);
    assert_eq!(guest.call(false, &[]), -1);
    assert_eq!(guest.capture(&mut vec![0; WASM_PAGE_BYTES + IMAGE_HEADER_BYTES], 1, 100), -1);
    assert_eq!(consume(&mut guest), 10);
    assert_eq!(guest.call(true, &[]), -1);
    assert_eq!(guest.call(false, &[]), 1);
    assert_eq!(consume(&mut guest), 11);
}

#[test]
fn same_blob_fresh_restore_resumes_without_init() {
    let mut guest = Guest::load(&blob()).unwrap();
    assert_eq!(guest.call(true, &[]), 1);
    assert_eq!(consume(&mut guest), 10);
    let mut image = vec![0; WASM_PAGE_BYTES + IMAGE_HEADER_BYTES];
    assert_eq!(guest.capture(&mut image, 1, 100), (WASM_PAGE_BYTES + IMAGE_HEADER_BYTES) as i32);
    assert_eq!(guest.call(false, &[]), 1);
    assert_eq!(consume(&mut guest), 11);
    assert_eq!(guest.call(false, &[]), 1);
    assert_eq!(consume(&mut guest), 12);
    assert_eq!(guest.restore(&image, 1, 100), 0);
    assert_eq!(guest.call(false, &[]), 1);
    assert_eq!(consume(&mut guest), 11);
    assert_eq!(guest.capture(&mut image, 1, 100), (WASM_PAGE_BYTES + IMAGE_HEADER_BYTES) as i32);
    assert_eq!(u32::from_le_bytes(image[IMAGE_HEADER_BYTES + 4..IMAGE_HEADER_BYTES + 8].try_into().unwrap()), 1);
}

#[test]
fn invalid_restore_preserves_live_state_and_trap_requires_restore() {
    let mut guest = Guest::load(&blob()).unwrap();
    assert_eq!(guest.call(true, &[]), 1);
    consume(&mut guest);
    let mut image = vec![0; WASM_PAGE_BYTES + IMAGE_HEADER_BYTES];
    guest.capture(&mut image, 1, 100);
    assert_eq!(guest.restore(&image[..image.len() - 1], 1, 100), -1);
    assert_eq!(guest.call(false, &[]), 1);
    assert_eq!(consume(&mut guest), 11);
    assert_eq!(guest.call(false, &[255]), -1);
    assert_eq!(guest.call(false, &[]), -1);
    assert_eq!(guest.capture(&mut vec![0; WASM_PAGE_BYTES + IMAGE_HEADER_BYTES], 1, 100), -1);
    assert_eq!(guest.restore(&image, 1, 100), 0);
    assert_eq!(guest.call(false, &[]), 1);
    assert_eq!(consume(&mut guest), 11);
}

#[test]
fn c_boundary_copies_bytes_and_refuses_invalid_buffers() {
    let blob = blob();
    unsafe {
        let mut error = [0; 256];
        let guest = halo_diax_create(blob.as_ptr(), blob.len() as u32, error.as_mut_ptr(), 256);
        assert!(!guest.is_null());
        assert_eq!(halo_diax_abi_version(guest), ABI_VERSION_PACKED);
        assert_eq!(halo_diax_call(guest, 1, ptr::null(), 1), -1);
        assert!(halo_diax_error(guest, error.as_mut_ptr(), 256) > 0);
        assert_eq!(halo_diax_call(guest, 1, ptr::null(), 0), 1);
        assert_eq!(halo_diax_take_output(guest, ptr::null_mut(), 0), -1);
        let mut actions = [0];
        assert_eq!(halo_diax_take_output(guest, actions.as_mut_ptr(), 1), 1);
        assert_eq!(actions, [10]);
        halo_diax_destroy(guest);
    }
}

#[test]
fn adapter_short_output_faults_until_checkpoint_rollback() {
    let mut guest = Guest::load(&blob()).unwrap();
    assert_eq!(guest.call(true, &[]), 1);
    consume(&mut guest);
    let mut image = vec![0; WASM_PAGE_BYTES + IMAGE_HEADER_BYTES];
    assert_eq!(guest.capture(&mut image, 1, 100), (WASM_PAGE_BYTES + IMAGE_HEADER_BYTES) as i32);
    assert_eq!(guest.event(&[], &mut []), -1);
    assert_eq!(guest.call(false, &[]), -1);
    assert_eq!(guest.take_output(&mut [0]), -1);
    assert_eq!(guest.capture(&mut image, 1, 100), -1);
    assert_eq!(guest.restore(&image, 1, 100), 0);
    let mut actions = [0];
    assert_eq!(guest.event(&[], &mut actions), 1);
    assert_eq!(actions, [11]);
}

#[test]
fn snapshot_identity_integrity_and_fence_refusals_preserve_live_state() {
    let mut guest = Guest::load(&blob()).unwrap();
    assert_eq!(guest.call(true, &[]), 1);
    consume(&mut guest);
    let mut image = vec![0; WASM_PAGE_BYTES + IMAGE_HEADER_BYTES];
    assert_eq!(guest.capture(&mut image, 7, 42), image.len() as i32);
    assert_eq!(guest.restore(&image, 7, 41), -1);
    assert_eq!(guest.restore(&image, 6, 42), -1);
    let mut corrupted = image.clone();
    corrupted[IMAGE_HEADER_BYTES] ^= 1;
    assert_eq!(guest.restore(&corrupted, 7, 42), -1);
    assert_eq!(guest.restore(&vec![0; image.len()], 7, 42), -1);
    let mut foreign = Guest::load(&fixture::blob_with_init_len(0)).unwrap();
    assert_eq!(foreign.call(true, &[]), 0);
    assert_eq!(foreign.take_output(&mut []), 0);
    assert_eq!(foreign.restore(&image, 7, 42), -1);
    assert_eq!(guest.call(false, &[]), 1);
    assert_eq!(consume(&mut guest), 11);
    assert_eq!(guest.restore(&image, 7, 42), 0);
    assert_eq!(guest.call(false, &[]), 1);
    assert_eq!(consume(&mut guest), 11);
}

#[test]
fn short_error_buffer_is_terminated_and_contains_reason() {
    let mut guest = Guest::load(&blob()).unwrap();
    assert_eq!(guest.call(false, &[]), -1);
    let mut error = [99; 8];
    unsafe {
        assert_eq!(halo_diax_error(&guest, error.as_mut_ptr(), 8), 7);
        assert_eq!(error[7], 0);
        assert_eq!(&error[..7], b"guest f");
        assert_eq!(halo_diax_error(&guest, ptr::null_mut(), 0), -1);
    }
}
