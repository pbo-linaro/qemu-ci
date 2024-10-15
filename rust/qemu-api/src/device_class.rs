// Copyright 2024, Linaro Limited
// Author(s): Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
// SPDX-License-Identifier: GPL-2.0-or-later

use std::ffi::CStr;

use crate::bindings::{self, Property};

#[macro_export]
macro_rules! device_class_init {
    ($func:ident, props => $props:ident, realize_fn => $realize_fn:expr, legacy_reset_fn => $legacy_reset_fn:expr, vmsd => $vmsd:ident$(,)*) => {
        #[no_mangle]
        pub unsafe extern "C" fn $func(
            klass: *mut $crate::bindings::ObjectClass,
            _: *mut ::std::os::raw::c_void,
        ) {
            let mut dc =
                ::core::ptr::NonNull::new(klass.cast::<$crate::bindings::DeviceClass>()).unwrap();
            unsafe {
                dc.as_mut().realize = $realize_fn;
                dc.as_mut().vmsd = &$vmsd;
                $crate::bindings::device_class_set_legacy_reset(dc.as_mut(), $legacy_reset_fn);
                $crate::bindings::device_class_set_props(dc.as_mut(), $props.as_mut_ptr());
            }
        }
    };
}

#[macro_export]
macro_rules! define_property {
    ($name:expr, $state:ty, $field:ident, $prop:expr, $type:expr, default = $defval:expr$(,)*) => {
        $crate::bindings::Property {
            name: {
                #[used]
                static _TEMP: &::std::ffi::CStr = $name;
                _TEMP.as_ptr()
            },
            info: $prop,
            offset: $crate::offset_of!($state, $field)
                .try_into()
                .expect("Could not fit offset value to type"),
            bitnr: 0,
            bitmask: 0,
            set_default: true,
            defval: $crate::bindings::Property__bindgen_ty_1 { u: $defval.into() },
            arrayoffset: 0,
            arrayinfo: ::core::ptr::null(),
            arrayfieldsize: 0,
            link_type: ::core::ptr::null(),
        }
    };
    ($name:expr, $state:ty, $field:ident, $prop:expr, $type:expr$(,)*) => {
        $crate::bindings::Property {
            name: {
                #[used]
                static _TEMP: &::std::ffi::CStr = $name;
                _TEMP.as_ptr()
            },
            info: $prop,
            offset: $crate::offset_of!($state, $field)
                .try_into()
                .expect("Could not fit offset value to type"),
            bitnr: 0,
            bitmask: 0,
            set_default: false,
            defval: $crate::bindings::Property__bindgen_ty_1 { i: 0 },
            arrayoffset: 0,
            arrayinfo: ::core::ptr::null(),
            arrayfieldsize: 0,
            link_type: ::core::ptr::null(),
        }
    };
}

#[repr(C)]
pub struct Properties<const N: usize>(pub Option<[Property; N]>, pub fn() -> [Property; N]);

impl<const N: usize> Properties<N> {
    pub fn as_mut_ptr(&mut self) -> *mut Property {
        match self.0 {
            None => { self.0 = Some(self.1()); },
            Some(_) => {},
        }
        self.0.as_mut().unwrap().as_mut_ptr()
    }
}

#[macro_export]
macro_rules! declare_properties {
    ($ident:ident, $($prop:expr),*$(,)*) => {

        const fn _calc_prop_len() -> usize {
            let mut len = 1;
            $({
                _ = stringify!($prop);
                len += 1;
            })*
            len
        }
        const PROP_LEN: usize = _calc_prop_len();

        fn _make_properties() -> [$crate::bindings::Property; PROP_LEN] {
            [
                $($prop),*,
                $crate::zeroable::Zeroable::ZERO,
            ]
        }

        #[no_mangle]
        pub static mut $ident: $crate::device_class::Properties<PROP_LEN> = $crate::device_class::Properties(None, _make_properties);
    };
}

#[macro_export]
macro_rules! vm_state_description {
    ($(#[$outer:meta])*
     $name:ident,
     $(name: $vname:expr,)*
     $(unmigratable: $um_val:expr,)*
    ) => {
        #[used]
        $(#[$outer])*
        pub static $name: $crate::bindings::VMStateDescription = $crate::bindings::VMStateDescription {
            $(name: {
                #[used]
                static VMSTATE_NAME: &::std::ffi::CStr = $vname;
                $vname.as_ptr()
            },)*
            unmigratable: true,
            ..$crate::zeroable::Zeroable::ZERO
        };
    }
}

// workaround until we can use --generate-cstr in bindgen.
pub const TYPE_SYS_BUS_DEVICE: &CStr =
    unsafe { CStr::from_bytes_with_nul_unchecked(bindings::TYPE_SYS_BUS_DEVICE) };
