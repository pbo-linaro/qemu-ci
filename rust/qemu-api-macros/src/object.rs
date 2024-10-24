// Copyright 2024, Linaro Limited
// Author(s): Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
// SPDX-License-Identifier: GPL-2.0-or-later

use proc_macro::TokenStream;
use quote::{format_ident, quote};
use syn::{parse_macro_input, DeriveInput};

use crate::utilities::*;

pub fn derive_object(input: TokenStream) -> TokenStream {
    let input = parse_macro_input!(input as DeriveInput);

    assert_is_repr_c_struct(&input, "Object");

    let name = input.ident;
    let module_static = format_ident!("__{}_LOAD_MODULE", name);

    let ctors = quote! {
        #[allow(non_upper_case_globals)]
        #[used]
        #[cfg_attr(target_os = "linux", link_section = ".ctors")]
        #[cfg_attr(target_os = "macos", link_section = "__DATA,__mod_init_func")]
        #[cfg_attr(target_os = "windows", link_section = ".CRT$XCU")]
        pub static #module_static: extern "C" fn() = {
            extern "C" fn __register() {
                unsafe {
                    ::qemu_api::bindings::type_register_static(&<#name as ::qemu_api::objects::ObjectImplUnsafe>::TYPE_INFO);
                }
            }

            extern "C" fn __load() {
                unsafe {
                    ::qemu_api::bindings::register_module_init(
                        Some(__register),
                        ::qemu_api::bindings::module_init_type::MODULE_INIT_QOM
                    );
                }
            }

            __load
        };
    };

    let instance_init = format_ident!("__{}_instance_init_generated", name);
    let instance_post_init = format_ident!("__{}_instance_post_init_generated", name);
    let instance_finalize = format_ident!("__{}_instance_finalize_generated", name);

    let obj_impl_unsafe = quote! {
        unsafe impl ::qemu_api::objects::ObjectImplUnsafe for #name {
            const TYPE_INFO: ::qemu_api::bindings::TypeInfo =
                ::qemu_api::bindings::TypeInfo {
                    name: <Self as ::qemu_api::objects::ObjectImpl>::TYPE_NAME.as_ptr(),
                    parent: if let Some(pname) = <Self as ::qemu_api::objects::ObjectImpl>::PARENT_TYPE_NAME {
                        pname.as_ptr()
                    } else {
                        ::core::ptr::null()
                    },
                    instance_size: ::core::mem::size_of::<Self>() as ::qemu_api::bindings::size_t,
                    instance_align: ::core::mem::align_of::<Self>() as ::qemu_api::bindings::size_t,
                    instance_init: <Self as ::qemu_api::objects::ObjectImplUnsafe>::INSTANCE_INIT,
                    instance_post_init: <Self as ::qemu_api::objects::ObjectImplUnsafe>::INSTANCE_POST_INIT,
                    instance_finalize: <Self as ::qemu_api::objects::ObjectImplUnsafe>::INSTANCE_FINALIZE,
                    abstract_: <Self as ::qemu_api::objects::ObjectImpl>::ABSTRACT,
                    class_size:  ::core::mem::size_of::<<Self as ::qemu_api::objects::ObjectImpl>::Class>() as ::qemu_api::bindings::size_t,
                    class_init: <<Self as ::qemu_api::objects::ObjectImpl>::Class as ::qemu_api::objects::ClassImplUnsafe>::CLASS_INIT,
                    class_base_init: <<Self as ::qemu_api::objects::ObjectImpl>::Class as ::qemu_api::objects::ClassImplUnsafe>::CLASS_BASE_INIT,
                    class_data: ::core::ptr::null_mut(),
                    interfaces: ::core::ptr::null_mut(),
                };
            const INSTANCE_INIT: Option<unsafe extern "C" fn(obj: *mut ::qemu_api::bindings::Object)> = Some(#instance_init);
            const INSTANCE_POST_INIT: Option<unsafe extern "C" fn(obj: *mut ::qemu_api::bindings::Object)> = Some(#instance_post_init);
            const INSTANCE_FINALIZE: Option<unsafe extern "C" fn(obj: *mut ::qemu_api::bindings::Object)> = Some(#instance_finalize);
        }

        #[no_mangle]
        pub unsafe extern "C" fn #instance_init(obj: *mut ::qemu_api::bindings::Object) {
            let mut instance = NonNull::new(obj.cast::<#name>()).expect(concat!("Expected obj to be a non-null pointer of type ", stringify!(#name)));
            unsafe {
                ::qemu_api::objects::ObjectImpl::instance_init(instance.as_mut());
            }
        }

        #[no_mangle]
        pub unsafe extern "C" fn #instance_post_init(obj: *mut ::qemu_api::bindings::Object) {
                let mut instance = NonNull::new(obj.cast::<#name>()).expect(concat!("Expected obj to be a non-null pointer of type ", stringify!(#name)));
            unsafe {
                ::qemu_api::objects::ObjectImpl::instance_post_init(instance.as_mut());
            }
        }

        #[no_mangle]
        pub unsafe extern "C" fn #instance_finalize(obj: *mut ::qemu_api::bindings::Object) {
                let mut instance = NonNull::new(obj.cast::<#name>()).expect(concat!("Expected obj to be a non-null pointer of type ", stringify!(#name)));
            unsafe {
                ::qemu_api::objects::ObjectImpl::instance_finalize(instance.as_mut());
            }
        }
    };

    let expanded = quote! {
        #obj_impl_unsafe

        #ctors
    };
    TokenStream::from(expanded)
}
