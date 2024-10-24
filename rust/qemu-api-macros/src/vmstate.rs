// Copyright 2024, Linaro Limited
// Author(s): Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
// SPDX-License-Identifier: GPL-2.0-or-later

use quote::{format_ident, quote};

pub fn make_vmstate(
    name: &syn::Ident,
    vmstate_fields: Option<syn::Expr>,
    vmstate_subsections: Option<syn::Expr>,
) -> (syn::Ident, proc_macro2::TokenStream) {
    let vmstate_description_ident = format_ident!("__VMSTATE_{}", name);

    let pre_load = format_ident!("__{}_pre_load_generated", name);
    let post_load = format_ident!("__{}_post_load_generated", name);
    let pre_save = format_ident!("__{}_pre_save_generated", name);
    let post_save = format_ident!("__{}_post_save_generated", name);
    let needed = format_ident!("__{}_needed_generated", name);
    let dev_unplug_pending = format_ident!("__{}_dev_unplug_pending_generated", name);

    let migrateable_fish = quote! {<#name as ::qemu_api::objects::Migrateable>};
    let vmstate_fields = if let Some(fields) = vmstate_fields {
        quote! {
            #fields
        }
    } else {
        quote! {
            ::core::ptr::null()
        }
    };
    let vmstate_subsections = if let Some(subsections) = vmstate_subsections {
        quote! {
            #subsections
        }
    } else {
        quote! {
            ::core::ptr::null()
        }
    };

    let vmstate_description = quote! {
        #[used]
        #[allow(non_upper_case_globals)]
        pub static #vmstate_description_ident: ::qemu_api::bindings::VMStateDescription = ::qemu_api::bindings::VMStateDescription {
            name: if let Some(name) = #migrateable_fish::NAME {
                name.as_ptr()
            } else {
                <#name as ::qemu_api::objects::ObjectImplUnsafe>::TYPE_INFO.name
            },
            unmigratable: #migrateable_fish::UNMIGRATABLE,
            early_setup: #migrateable_fish::EARLY_SETUP,
            version_id: #migrateable_fish::VERSION_ID,
            minimum_version_id: #migrateable_fish::MINIMUM_VERSION_ID,
            priority: #migrateable_fish::PRIORITY,
            pre_load: Some(#pre_load),
            post_load: Some(#post_load),
            pre_save: Some(#pre_save),
            post_save: Some(#post_save),
            needed: Some(#needed),
            dev_unplug_pending: Some(#dev_unplug_pending),
            fields: #vmstate_fields,
            subsections: #vmstate_subsections,
        };

        #[no_mangle]
        pub unsafe extern "C" fn #pre_load(opaque: *mut ::core::ffi::c_void) -> ::core::ffi::c_int {
            let mut instance = NonNull::new(opaque.cast::<#name>()).expect(concat!("Expected opaque to be a non-null pointer of type ", stringify!(#name), "::Object"));
            unsafe {
                ::qemu_api::objects::Migrateable::pre_load(instance.as_mut())
            }
        }
        #[no_mangle]
        pub unsafe extern "C" fn #post_load(opaque: *mut ::core::ffi::c_void, version_id: core::ffi::c_int) -> ::core::ffi::c_int {
            let mut instance = NonNull::new(opaque.cast::<#name>()).expect(concat!("Expected opaque to be a non-null pointer of type ", stringify!(#name), "::Object"));
            unsafe {
                ::qemu_api::objects::Migrateable::post_load(instance.as_mut(), version_id)
            }
        }
        #[no_mangle]
        pub unsafe extern "C" fn #pre_save(opaque: *mut ::core::ffi::c_void) -> ::core::ffi::c_int {
            let mut instance = NonNull::new(opaque.cast::<#name>()).expect(concat!("Expected opaque to be a non-null pointer of type ", stringify!(#name), "::Object"));
            unsafe {
                ::qemu_api::objects::Migrateable::pre_save(instance.as_mut())
            }
        }
        #[no_mangle]
        pub unsafe extern "C" fn #post_save(opaque: *mut ::core::ffi::c_void) -> ::core::ffi::c_int {
            let mut instance = NonNull::new(opaque.cast::<#name>()).expect(concat!("Expected opaque to be a non-null pointer of type ", stringify!(#name), "::Object"));
            unsafe {
                ::qemu_api::objects::Migrateable::post_save(instance.as_mut())
            }
        }
        #[no_mangle]
        pub unsafe extern "C" fn #needed(opaque: *mut ::core::ffi::c_void) -> bool {
            let mut instance = NonNull::new(opaque.cast::<#name>()).expect(concat!("Expected opaque to be a non-null pointer of type ", stringify!(#name), "::Object"));
            unsafe {
                ::qemu_api::objects::Migrateable::needed(instance.as_mut())
            }
        }
        #[no_mangle]
        pub unsafe extern "C" fn #dev_unplug_pending(opaque: *mut ::core::ffi::c_void) -> bool {
            let mut instance = NonNull::new(opaque.cast::<#name>()).expect(concat!("Expected opaque to be a non-null pointer of type ", stringify!(#name), "::Object"));
            unsafe {
                ::qemu_api::objects::Migrateable::dev_unplug_pending(instance.as_mut())
            }
        }
    };

    let expanded = quote! {
        #vmstate_description
    };
    (vmstate_description_ident, expanded)
}
