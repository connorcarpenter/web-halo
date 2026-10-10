//! Worker-owned byte boundary over the existing Diax runtime. No game policy.
//! Every C call, including destruction, must run on the owning simulation worker.

use modding_abi::{EXPORT_ABI_VERSION, EXPORT_INIT, EXPORT_ON_EVENT};
use modding_runtime::{ModRuntime, MAX_GUEST_MEMORY_BYTES, WASM_PAGE_BYTES};
use sha2::{Digest, Sha256};
use std::{marker::PhantomData, ptr, rc::Rc, slice};

const IMAGE_HEADER_BYTES: usize = 84;
const MAX_IMAGE_BYTES: usize = IMAGE_HEADER_BYTES + MAX_GUEST_MEMORY_BYTES;
const IMAGE_MAGIC: &[u8; 8] = b"HDXIMG01";

pub struct Guest {
    blob: Vec<u8>,
    runtime: ModRuntime,
    abi: u32,
    initialized: bool,
    faulted: bool,
    pending: Option<Vec<u8>>,
    error: String,
    // A guest cannot be moved between workers or shared by Rust callers.
    owner: PhantomData<Rc<()>>,
}

impl Guest {
    fn load(blob: &[u8]) -> Result<Self, String> {
        let mut runtime = ModRuntime::load(blob).map_err(|e| e.to_string())?;
        let abi = runtime
            .read_u32_export(EXPORT_ABI_VERSION)
            .map_err(|e| e.to_string())?;
        runtime.describe_schema().map_err(|e| e.to_string())?;
        Ok(Self {
            blob: blob.to_vec(),
            runtime,
            abi,
            initialized: false,
            faulted: false,
            pending: None,
            error: String::new(),
            owner: PhantomData,
        })
    }

    fn refuse(&mut self, error: impl ToString) -> i32 {
        self.error = error.to_string();
        -1
    }

    fn call(&mut self, init: bool, input: &[u8]) -> i32 {
        if self.faulted || self.pending.is_some() || init == self.initialized {
            return self.refuse("guest faulted, output outstanding, or invalid init/event order");
        }
        match self
            .runtime
            .call_export(if init { EXPORT_INIT } else { EXPORT_ON_EVENT }, input)
        {
            Ok(output) => {
                // Retain ALL action bytes, including __init actions. A short
                // C output buffer never reruns a guest or discards its actions.
                let Ok(len) = i32::try_from(output.len()) else {
                    self.faulted = true;
                    return self.refuse("guest output exceeds C byte-count range");
                };
                self.pending = Some(output);
                self.initialized = true;
                self.error.clear();
                len
            }
            Err(error) => {
                // A trap can follow mutations. Never retry that event as if the
                // guest were unchanged, or checkpoint its incomplete state.
                self.faulted = true;
                self.refuse(error)
            }
        }
    }

    fn take_output(&mut self, out: &mut [u8]) -> i32 {
        if self.faulted {
            return self.refuse("faulted guest output cannot be applied; restore required");
        }
        let Some(bytes) = self.pending.as_ref() else {
            return self.refuse("no guest output outstanding");
        };
        if out.len() < bytes.len() {
            return self.refuse("output buffer too small; action bytes retained");
        }
        out[..bytes.len()].copy_from_slice(bytes);
        let len = bytes.len() as i32;
        self.pending = None;
        self.error.clear();
        len
    }

    fn capture(&mut self, out: &mut [u8], generation: i32, serial: u32) -> i32 {
        if !self.initialized || self.faulted || self.pending.is_some() {
            return self.refuse("capture requires a completed call with output consumed");
        }
        if generation < 0 {
            return self.refuse("invalid checkpoint generation");
        }
        let image = self.runtime.memory_image();
        let Some(total) = image.len().checked_add(IMAGE_HEADER_BYTES) else {
            return self.refuse("image length overflow");
        };
        let Ok(count) = i32::try_from(total) else {
            return self.refuse("image exceeds C byte-count range");
        };
        if total > MAX_IMAGE_BYTES || out.len() < total {
            return self.refuse("image buffer too small");
        }
        out[..8].copy_from_slice(IMAGE_MAGIC);
        out[8..40].copy_from_slice(&Sha256::digest(&self.blob));
        out[40..44].copy_from_slice(&generation.to_le_bytes());
        out[44..48].copy_from_slice(&serial.to_le_bytes());
        out[48..52].copy_from_slice(&(image.len() as u32).to_le_bytes());
        out[IMAGE_HEADER_BYTES..total].copy_from_slice(&image);
        let mut hash = Sha256::new();
        hash.update(&out[..52]);
        hash.update(&image);
        out[52..84].copy_from_slice(&hash.finalize());
        self.error.clear();
        count
    }

    fn event(&mut self, input: &[u8], out: &mut [u8]) -> i32 {
        let produced = self.call(false, input);
        if produced < 0 {
            return produced;
        }
        if out.len() < produced as usize {
            // The serial did not advance, but guest state may have. Never
            // execute this event twice or apply its uncommitted batch later.
            self.faulted = true;
            return self.refuse("action exceeds adapter output buffer; guest faulted");
        }
        self.take_output(out)
    }

    fn restore(&mut self, envelope: &[u8], generation: i32, serial: u32) -> i32 {
        if !self.initialized || (self.pending.is_some() && !self.faulted) {
            return self.refuse("restore requires an initialized guest with output consumed");
        }
        if envelope.len() < IMAGE_HEADER_BYTES || envelope.len() > MAX_IMAGE_BYTES {
            return self.refuse("invalid checkpoint envelope length");
        }
        let image = &envelope[IMAGE_HEADER_BYTES..];
        let declared = u32::from_le_bytes(envelope[48..52].try_into().unwrap()) as usize;
        let source_generation = i32::from_le_bytes(envelope[40..44].try_into().unwrap());
        let source_serial = u32::from_le_bytes(envelope[44..48].try_into().unwrap());
        let mut hash = Sha256::new();
        hash.update(&envelope[..52]);
        hash.update(image);
        if &envelope[..8] != IMAGE_MAGIC || envelope[8..40] != Sha256::digest(&self.blob)[..]
            || generation < 0 || source_generation != generation || source_serial != serial
            || declared != image.len() || image.is_empty()
            || !image.len().is_multiple_of(WASM_PAGE_BYTES)
            || envelope[52..84] != hash.finalize()[..]
        {
            return self.refuse("checkpoint identity, fence or integrity mismatch");
        }
        // Stage from the exact stored blob; never initialize after restore and
        // never replace live state on a load/image validation refusal.
        let staged = ModRuntime::load(&self.blob).and_then(|mut runtime| {
            runtime.restore_memory_image(image)?;
            Ok(runtime)
        });
        match staged {
            Ok(runtime) => {
                self.runtime = runtime;
                self.faulted = false;
                self.pending = None;
                self.error.clear();
                0
            }
            Err(error) => self.refuse(error),
        }
    }
}

// SAFETY contract is shared with diax_guest.h: nonzero lengths require valid
// disjoint buffers for the whole call; handles have one serialized owner.
unsafe fn input<'a>(data: *const u8, len: u32) -> Option<&'a [u8]> {
    if len == 0 {
        Some(&[])
    } else if data.is_null() || len as usize > isize::MAX as usize {
        None
    } else {
        Some(slice::from_raw_parts(data, len as usize))
    }
}
unsafe fn output<'a>(data: *mut u8, len: u32) -> Option<&'a mut [u8]> {
    if len == 0 {
        Some(&mut [])
    } else if data.is_null() || len as usize > isize::MAX as usize {
        None
    } else {
        Some(slice::from_raw_parts_mut(data, len as usize))
    }
}

/// # Safety
/// Buffers obey the header's validity, length and disjointness contract.
#[no_mangle]
pub unsafe extern "C" fn halo_diax_create(
    blob: *const u8,
    len: u32,
    error: *mut u8,
    cap: u32,
) -> *mut Guest {
    let Some(blob) = input(blob, len) else {
        return ptr::null_mut();
    };
    match Guest::load(blob) {
        Ok(guest) => Box::into_raw(Box::new(guest)),
        Err(message) => {
            if let Some(out) = output(error, cap) {
                if !out.is_empty() {
                    let len = (out.len() - 1).min(message.len());
                    out[..len].copy_from_slice(&message.as_bytes()[..len]);
                    out[len] = 0;
                }
            }
            ptr::null_mut()
        }
    }
}

/// # Safety
/// The handle is live, owned by this worker, and destroyed exactly once.
#[no_mangle]
pub unsafe extern "C" fn halo_diax_destroy(guest: *mut Guest) {
    if !guest.is_null() {
        drop(Box::from_raw(guest));
    }
}

/// # Safety
/// The handle is live and owned by this worker.
#[no_mangle]
pub unsafe extern "C" fn halo_diax_abi_version(guest: *const Guest) -> u32 {
    guest.as_ref().map_or(0, |g| g.abi)
}

/// # Safety
/// Live worker-owned handle and valid input buffer; no concurrent calls.
#[no_mangle]
pub unsafe extern "C" fn halo_diax_call(
    guest: *mut Guest,
    init: i32,
    bytes: *const u8,
    len: u32,
) -> i32 {
    let Some(guest) = guest.as_mut() else {
        return -1;
    };
    let Some(bytes) = input(bytes, len) else {
        return guest.refuse("invalid input buffer");
    };
    if init != 0 && init != 1 {
        return guest.refuse("invalid call selector");
    }
    guest.call(init == 1, bytes)
}

/// # Safety
/// Live worker-owned handle; output is valid and disjoint from the handle.
#[no_mangle]
pub unsafe extern "C" fn halo_diax_take_output(guest: *mut Guest, bytes: *mut u8, cap: u32) -> i32 {
    let Some(guest) = guest.as_mut() else {
        return -1;
    };
    let Some(out) = output(bytes, cap) else {
        return guest.refuse("invalid output buffer");
    };
    guest.take_output(out)
}

/// # Safety
/// Live worker-owned handle; output is valid and disjoint from the handle.
#[no_mangle]
pub unsafe extern "C" fn halo_diax_capture(
    guest: *mut Guest, bytes: *mut u8, cap: u32, generation: i32, serial: u32,
) -> i32 {
    let Some(guest) = guest.as_mut() else {
        return -1;
    };
    let Some(out) = output(bytes, cap) else {
        return guest.refuse("invalid image buffer");
    };
    guest.capture(out, generation, serial)
}

/// # Safety
/// Live worker-owned handle and valid image buffer; no concurrent calls.
#[no_mangle]
pub unsafe extern "C" fn halo_diax_restore(
    guest: *mut Guest, bytes: *const u8, len: u32, generation: i32, serial: u32,
) -> i32 {
    let Some(guest) = guest.as_mut() else {
        return -1;
    };
    let Some(image) = input(bytes, len) else {
        return guest.refuse("invalid image buffer");
    };
    guest.restore(image, generation, serial)
}

/// # Safety
/// Live worker-owned handle; output is valid and disjoint from the handle.
#[no_mangle]
pub unsafe extern "C" fn halo_diax_error(guest: *const Guest, bytes: *mut u8, cap: u32) -> i32 {
    let Some(guest) = guest.as_ref() else {
        return -1;
    };
    let Some(out) = output(bytes, cap) else {
        return -1;
    };
    if out.is_empty() {
        return -1;
    }
    let mut len = (out.len() - 1).min(guest.error.len());
    while !guest.error.is_char_boundary(len) { len -= 1; }
    out[..len].copy_from_slice(&guest.error.as_bytes()[..len]);
    out[len] = 0;
    i32::try_from(len).unwrap_or(-1)
}

/// # Safety
/// Live worker-owned handle; input and output are valid disjoint buffers.
#[no_mangle]
pub unsafe extern "C" fn halo_diax_event(
    guest: *mut Guest,
    bytes: *const u8,
    len: u32,
    actions: *mut u8,
    capacity: u32,
) -> i32 {
    let Some(guest) = guest.as_mut() else {
        return -1;
    };
    let Some(input) = input(bytes, len) else {
        return guest.refuse("invalid input buffer");
    };
    let Some(out) = output(actions, capacity) else {
        return guest.refuse("invalid output buffer");
    };
    guest.event(input, out)
}

#[cfg(test)]
mod tests;
