// SPDX-License-Identifier: GPL-2.0-or-later

use std::ptr;

/// This trait provides a replacement for core::mem::zeroed() that can be
/// used as a `const fn` prior to Rust 1.75.0.  As an added bonus it removes
/// usage of `unsafe` blocks.
///
/// Unlike other Zeroable traits found in other crates (e.g.
/// [`pinned_init`](https://docs.rs/pinned-init/latest/pinned_init/trait.Zeroable.html))
/// this is a safe trait because the value `ZERO` constant has to be written by
/// hand.  The `pinned_init` crate instead makes the trait unsafe, but it
/// provides a `#[derive(Zeroable)]` macro to define it with compile-time
/// safety checks. Once we can assume Rust 1.75.0 is available, we could
/// switch to their idea, and use `core::mem::zeroed()` to provide a blanked
/// implementation of the `ZERO` constant.
pub trait Zeroable: Default {
    const ZERO: Self;
}

impl Zeroable for crate::bindings::Property__bindgen_ty_1 {
    const ZERO: Self = Self { i: 0 };
}

impl Zeroable for crate::bindings::Property {
    const ZERO: Self = Self {
        name: ptr::null(),
        info: ptr::null(),
        offset: 0,
        bitnr: 0,
        bitmask: 0,
        set_default: false,
        defval: Zeroable::ZERO,
        arrayoffset: 0,
        arrayinfo: ptr::null(),
        arrayfieldsize: 0,
        link_type: ptr::null(),
    };
}

impl Zeroable for crate::bindings::VMStateDescription {
    const ZERO: Self = Self {
        name: ptr::null(),
        unmigratable: false,
        early_setup: false,
        version_id: 0,
        minimum_version_id: 0,
        priority: crate::bindings::MigrationPriority::MIG_PRI_DEFAULT,
        pre_load: None,
        post_load: None,
        pre_save: None,
        post_save: None,
        needed: None,
        dev_unplug_pending: None,
        fields: ptr::null(),
        subsections: ptr::null(),
    };
}

impl Zeroable for crate::bindings::MemoryRegionOps__bindgen_ty_1 {
    const ZERO: Self = Self {
        min_access_size: 0,
        max_access_size: 0,
        unaligned: false,
        accepts: None,
    };
}

impl Zeroable for crate::bindings::MemoryRegionOps__bindgen_ty_2 {
    const ZERO: Self = Self {
        min_access_size: 0,
        max_access_size: 0,
        unaligned: false,
    };
}
