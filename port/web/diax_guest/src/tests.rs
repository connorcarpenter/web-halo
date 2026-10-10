use super::*;
use modding_abi::{encode, ParamSchema, ABI_VERSION_PACKED};
use modding_runtime::WASM_PAGE_BYTES;

// Real interpreted Wasm, deliberately vocabulary-free boundary controls.
// Persistent counter, init count and allocator state live in linear memory.
fn blob() -> Vec<u8> {
    let schema = encode(&ParamSchema::new(1));
    let escaped: String = schema.iter().map(|b| format!("\\{b:02x}")).collect();
    wat::parse_str(format!(
        r#"(module
      (memory (export "memory") 1)
      (data (i32.const 16) "{escaped}")
      (func (export "__abi_version") (result i32) (i32.const {abi}))
      (func (export "__alloc") (param i32) (result i32) (i32.const 512))
      (func (export "__dealloc") (param i32 i32))
      (func (export "__describe_schema") (result i64) (i64.const {schema_slice}))
      (func $emit (result i64)
        (i32.store8 (i32.const 256) (i32.load (i32.const 0)))
        (i64.const {result_slice}))
      (func (export "__init") (param i32 i32) (result i64)
        (i32.store (i32.const 4) (i32.add (i32.load (i32.const 4)) (i32.const 1)))
        (i32.store (i32.const 0) (i32.const 10)) (call $emit))
      (func (export "__on_event") (param $ptr i32) (param $len i32) (result i64)
        (i32.store (i32.const 0) (i32.add (i32.load (i32.const 0)) (i32.const 1)))
        (if (i32.gt_u (local.get $len) (i32.const 0))
          (then (if (i32.eq (i32.load8_u (local.get $ptr)) (i32.const 255))
            (then unreachable))))
        (call $emit)))"#,
        abi = ABI_VERSION_PACKED,
        schema_slice = modding_abi::pack_slice(16, schema.len() as u32),
        result_slice = modding_abi::pack_slice(256, 1),
    ))
    .unwrap()
}

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
    assert_eq!(guest.capture(&mut vec![0; WASM_PAGE_BYTES]), -1);
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
    let mut image = vec![0; WASM_PAGE_BYTES];
    assert_eq!(guest.capture(&mut image), WASM_PAGE_BYTES as i32);
    assert_eq!(guest.call(false, &[]), 1);
    assert_eq!(consume(&mut guest), 11);
    assert_eq!(guest.call(false, &[]), 1);
    assert_eq!(consume(&mut guest), 12);
    assert_eq!(guest.restore(&image), 0);
    assert_eq!(guest.call(false, &[]), 1);
    assert_eq!(consume(&mut guest), 11);
    assert_eq!(guest.capture(&mut image), WASM_PAGE_BYTES as i32);
    assert_eq!(u32::from_le_bytes(image[4..8].try_into().unwrap()), 1);
}

#[test]
fn invalid_restore_preserves_live_state_and_trap_requires_restore() {
    let mut guest = Guest::load(&blob()).unwrap();
    assert_eq!(guest.call(true, &[]), 1);
    consume(&mut guest);
    let mut image = vec![0; WASM_PAGE_BYTES];
    guest.capture(&mut image);
    assert_eq!(guest.restore(&image[..image.len() - 1]), -1);
    assert_eq!(guest.call(false, &[]), 1);
    assert_eq!(consume(&mut guest), 11);
    assert_eq!(guest.call(false, &[255]), -1);
    assert_eq!(guest.call(false, &[]), -1);
    assert_eq!(guest.capture(&mut vec![0; WASM_PAGE_BYTES]), -1);
    assert_eq!(guest.restore(&image), 0);
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
