//! Worker-owned byte boundary over the existing Diax runtime. No game policy.
//! Every C call, including destruction, must run on the owning simulation worker.

use modding_abi::{EXPORT_ABI_VERSION, EXPORT_INIT, EXPORT_ON_EVENT};
use modding_runtime::ModRuntime;
use std::{marker::PhantomData, ptr, rc::Rc, slice};

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

    fn capture(&mut self, out: &mut [u8]) -> i32 {
        if !self.initialized || self.faulted || self.pending.is_some() {
            return self.refuse("capture requires a completed call with output consumed");
        }
        let image = self.runtime.memory_image();
        if out.len() < image.len() {
            return self.refuse("image buffer too small");
        }
        out[..image.len()].copy_from_slice(&image);
        self.error.clear();
        image.len() as i32
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

    fn restore(&mut self, image: &[u8]) -> i32 {
        if !self.initialized || (self.pending.is_some() && !self.faulted) {
            return self.refuse("restore requires an initialized guest with output consumed");
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
pub unsafe extern "C" fn halo_diax_capture(guest: *mut Guest, bytes: *mut u8, cap: u32) -> i32 {
    let Some(guest) = guest.as_mut() else {
        return -1;
    };
    let Some(out) = output(bytes, cap) else {
        return guest.refuse("invalid image buffer");
    };
    guest.capture(out)
}

/// # Safety
/// Live worker-owned handle and valid image buffer; no concurrent calls.
#[no_mangle]
pub unsafe extern "C" fn halo_diax_restore(guest: *mut Guest, bytes: *const u8, len: u32) -> i32 {
    let Some(guest) = guest.as_mut() else {
        return -1;
    };
    let Some(image) = input(bytes, len) else {
        return guest.refuse("invalid image buffer");
    };
    guest.restore(image)
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
    if out.len() <= guest.error.len() {
        return -1;
    }
    out[..guest.error.len()].copy_from_slice(guest.error.as_bytes());
    out[guest.error.len()] = 0;
    guest.error.len() as i32
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
