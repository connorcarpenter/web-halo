use modding_abi::{encode, ParamSchema, ABI_VERSION_PACKED};

// Real interpreted Wasm, deliberately vocabulary-free boundary controls.
// Persistent counter, init count and allocator state live in linear memory.
pub fn blob() -> Vec<u8> {
    blob_with_init_len(1)
}

pub fn blob_with_init_len(init_len: u32) -> Vec<u8> {
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
        (i32.store (i32.const 0) (i32.const 10))
        (drop (call $emit)) (i64.const {init_slice}))
      (func (export "__on_event") (param $ptr i32) (param $len i32) (result i64)
        (i32.store (i32.const 0) (i32.add (i32.load (i32.const 0)) (i32.const 1)))
        (if (i32.gt_u (local.get $len) (i32.const 0))
          (then (if (i32.eq (i32.load8_u (local.get $ptr)) (i32.const 255))
            (then unreachable))))
        (call $emit)))"#,
        abi = ABI_VERSION_PACKED,
        schema_slice = modding_abi::pack_slice(16, schema.len() as u32),
        result_slice = modding_abi::pack_slice(256, 1),
        init_slice = modding_abi::pack_slice(256, init_len),
    ))
    .unwrap()
}
