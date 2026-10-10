//! Generates the same real Wasm controls used by the Rust runtime tests.
#[path = "../src/control_fixture.rs"]
mod fixture;

use std::{fs::File, io::Write};

fn main() -> std::io::Result<()> {
    let path = std::env::args()
        .nth(1)
        .expect("output header path required");
    let mut out = File::create(path)?;
    writeln!(
        out,
        "#define WORKER_CONTROL_ABI {}",
        modding_abi::ABI_VERSION_PACKED
    )?;
    for (name, blob) in [
        ("worker_control_blob", fixture::blob()),
        ("worker_empty_init_blob", fixture::blob_with_init_len(0)),
        (
            "worker_oversized_init_blob",
            fixture::blob_with_init_len(4097),
        ),
    ] {
        writeln!(out, "static const unsigned char {name}[] = {{")?;
        for byte in blob {
            write!(out, "{byte},")?;
        }
        writeln!(out, "}};")?;
    }
    Ok(())
}
