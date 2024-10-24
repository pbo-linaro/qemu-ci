// Copyright 2024, Linaro Limited
// Author(s): Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
// SPDX-License-Identifier: GPL-2.0-or-later

//! Implementation traits for QEMU objects, devices.

use ::core::ffi::{c_int, c_void, CStr};

use crate::bindings::{DeviceState, Error, MigrationPriority, Object, ObjectClass, TypeInfo};

/// Trait a type must implement to be registered with QEMU.
pub trait ObjectImpl {
    type Class: ClassImpl;
    const TYPE_NAME: &'static CStr;
    const PARENT_TYPE_NAME: Option<&'static CStr>;
    const ABSTRACT: bool;

    unsafe fn instance_init(&mut self) {}
    fn instance_post_init(&mut self) {}
    fn instance_finalize(&mut self) {}
}

/// The `extern`/`unsafe` analogue of [`ObjectImpl`]; it is used internally by `#[derive(Object)]`
/// and should not be implemented manually.
pub unsafe trait ObjectImplUnsafe {
    const TYPE_INFO: TypeInfo;

    const INSTANCE_INIT: Option<unsafe extern "C" fn(obj: *mut Object)>;
    const INSTANCE_POST_INIT: Option<unsafe extern "C" fn(obj: *mut Object)>;
    const INSTANCE_FINALIZE: Option<unsafe extern "C" fn(obj: *mut Object)>;
}

/// Methods for QOM class types.
pub trait ClassImpl {
    type Object: ObjectImpl;

    unsafe fn class_init(&mut self, _data: *mut core::ffi::c_void) {}
    unsafe fn class_base_init(&mut self, _data: *mut core::ffi::c_void) {}
}

/// The `extern`/`unsafe` analogue of [`ClassImpl`]; it is used internally by `#[derive(Object)]`
/// and should not be implemented manually.
pub unsafe trait ClassImplUnsafe {
    const CLASS_INIT: Option<unsafe extern "C" fn(klass: *mut ObjectClass, data: *mut c_void)>;
    const CLASS_BASE_INIT: Option<
        unsafe extern "C" fn(klass: *mut ObjectClass, data: *mut c_void),
    >;
}

/// Implementation methods for device types.
pub trait DeviceImpl: ObjectImpl {
    fn realize(&mut self) {}
    fn reset(&mut self) {}
}

/// The `extern`/`unsafe` analogue of [`DeviceImpl`]; it is used internally by `#[derive(Device)]`
/// and should not be implemented manually.
pub unsafe trait DeviceImplUnsafe {
    const REALIZE: Option<unsafe extern "C" fn(dev: *mut DeviceState, _errp: *mut *mut Error)>;
    const RESET: Option<unsafe extern "C" fn(dev: *mut DeviceState)>;
}

/// Constant metadata and implementation methods for types with device migration state.
pub trait Migrateable: DeviceImplUnsafe {
    const NAME: Option<&'static CStr> = None;
    const UNMIGRATABLE: bool = true;
    const EARLY_SETUP: bool = false;
    const VERSION_ID: c_int = 1;
    const MINIMUM_VERSION_ID: c_int = 1;
    const PRIORITY: MigrationPriority = MigrationPriority::MIG_PRI_DEFAULT;

    unsafe fn pre_load(&mut self) -> c_int {
        0
    }
    unsafe fn post_load(&mut self, _version_id: c_int) -> c_int {
        0
    }
    unsafe fn pre_save(&mut self) -> c_int {
        0
    }
    unsafe fn post_save(&mut self) -> c_int {
        0
    }
    unsafe fn needed(&mut self) -> bool {
        false
    }
    unsafe fn dev_unplug_pending(&mut self) -> bool {
        false
    }
}
