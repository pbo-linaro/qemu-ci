// SPDX-License-Identifier: GPL-2.0-or-later
#![allow(
    dead_code,
    improper_ctypes_definitions,
    improper_ctypes,
    non_camel_case_types,
    non_snake_case,
    non_upper_case_globals,
    unsafe_op_in_unsafe_fn,
    clippy::pedantic,
    clippy::restriction,
    clippy::style,
    clippy::missing_const_for_fn,
    clippy::useless_transmute,
    clippy::missing_safety_doc
)]

#[cfg(all(MESON, not(feature="system")))]
include!("bindings_tools.inc.rs");
#[cfg(all(MESON, feature="system"))]
include!("bindings_system.inc.rs");

#[cfg(all(not(MESON), not(feature="system")))]
include!(concat!(env!("OUT_DIR"), "/bindings_tools.inc.rs"));
#[cfg(all(not(MESON), feature="system"))]
include!(concat!(env!("OUT_DIR"), "/bindings_system.inc.rs"));

unsafe impl Send for Property {}
unsafe impl Sync for Property {}
unsafe impl Sync for TypeInfo {}

#[cfg(feature="system")]
unsafe impl Sync for VMStateDescription {}
#[cfg(feature="system")]
unsafe impl Sync for VMStateField {}
#[cfg(feature="system")]
unsafe impl Sync for VMStateInfo {}
