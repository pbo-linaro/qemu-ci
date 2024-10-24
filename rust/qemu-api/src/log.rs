// Copyright 2024, Linaro Limited
// Author(s): Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
// SPDX-License-Identifier: GPL-2.0-or-later

//! Logging functionality.
//!
//! This module provides:
//!
//! - a [`LogMask`] enum type that uses the mask values from the generated
//!   bindings, and makes sures the rust enum variant names and values will
//!   match.
//! - [`LogMask`] aliases [`LogMask::GUEST_ERROR`] and [`LogMask::UNIMPLEMENTED`]
//!   for convenience.
//! - a private `qemu_loglevel_mask()` function, counterpart of
//!   `qemu_loglevel_mask` in `include/qemu/log-for-trace.h`, which we
//!   cannot use from bindgen since it's a `static inline` item.
//! - public [`qemu_log`], [`qemu_log_mask`] and [`qemu_log_mask_and_addr`] functions that act like
//!   the C equivalents.
//!
//! # Examples
//!
//! ```rust
//! # use qemu_api::log::*;
//! # fn main() {
//! qemu_log_mask(LogMask::GUEST_ERROR, "device XYZ failed spectacularly");
//!
//! qemu_log_mask(
//!  LogMask::UNIMPLEMENTED,
//!  &format!(
//!    "We haven't implemented this feature in {file}:{line} out of pure laziness.",
//!    file = file!(),
//!    line = line!()
//!  )
//! );
//! # }
//! ```

use crate::bindings;

macro_rules! mask_variants {
    ($(#[$outer:meta])*
     pub enum $name:ident {
         $(
             $(#[$attrs:meta])*
             $symbol:ident
         ),*$(,)*
     }) => {
        $(#[$outer])*
            pub enum $name {
                $(
                    $(#[$attrs])*
                    $symbol = bindings::$symbol
                ),*
            }
    };
}

mask_variants! {
    /// A wrapper type for the various log mask `#defines` in the C code base.
    #[allow(non_camel_case_types)]
    #[derive(Copy, Clone, Eq, PartialEq, Debug)]
    #[repr(u32)]
    pub enum LogMask {
        CPU_LOG_TB_OUT_ASM,
        CPU_LOG_TB_IN_ASM,
        CPU_LOG_TB_OP,
        CPU_LOG_TB_OP_OPT,
        CPU_LOG_INT,
        CPU_LOG_EXEC,
        CPU_LOG_PCALL,
        CPU_LOG_TB_CPU,
        CPU_LOG_RESET,
        LOG_UNIMP,
        LOG_GUEST_ERROR,
        CPU_LOG_MMU,
        CPU_LOG_TB_NOCHAIN,
        CPU_LOG_PAGE,
        LOG_TRACE,
        CPU_LOG_TB_OP_IND,
        CPU_LOG_TB_FPU,
        CPU_LOG_PLUGIN,
        /// For user-mode strace logging.
        LOG_STRACE,
        LOG_PER_THREAD,
        CPU_LOG_TB_VPU,
        LOG_TB_OP_PLUGIN,
    }
}

impl LogMask {
    /// Alias.
    pub const GUEST_ERROR: Self = LogMask::LOG_GUEST_ERROR;
    /// Alias.
    pub const UNIMPLEMENTED: Self = LogMask::LOG_UNIMP;
}

/// Returns `true` if a bit is set in the current loglevel mask.
///
/// Counterpart of `qemu_loglevel_mask` in `include/qemu/log-for-trace.h`.
fn qemu_loglevel_mask(mask: LogMask) -> bool {
    // SAFETY: This is an internal global variable. We only read from it and reading invalid values
    // is not a concern here.
    let current_level = unsafe { bindings::qemu_loglevel };
    let mask = mask as ::core::ffi::c_int;

    (current_level & mask) != 0
}

/// Log a message in QEMU's log, given a specific log mask.
pub fn qemu_log_mask(log_mask: LogMask, str: &str) {
    if qemu_loglevel_mask(log_mask) {
        qemu_log(str);
    }
}

/// Log a message in QEMU's log only if a bit is set on the current loglevel mask and we are in the
/// address range we care about.
pub fn qemu_log_mask_and_addr(log_mask: LogMask, address: u64, str: &str) {
    if qemu_loglevel_mask(log_mask) && {
        // SAFETY: This function reads global variables/system state but an error here is not a
        // concern.
        unsafe { bindings::qemu_log_in_addr_range(address) }
    } {
        qemu_log(str);
    }
}

/// Log a message in QEMU's log, without a log mask.
pub fn qemu_log(str: &str) {
    let Ok(cstr) = ::std::ffi::CString::new(str) else {
        panic!(
            "qemu_log_mask: Converting passed string {:?} to CString failed.",
            str
        );
    };
    // SAFETY: We're passing two valid CStr pointers. The second argument for the variadic
    // `qemu_log` function must be a `*const c_char` since the format specifier is `%s`.
    // Therefore this is a safe call.
    unsafe { bindings::qemu_log(c"%s\n".as_ptr(), cstr.as_ptr()) };
}
