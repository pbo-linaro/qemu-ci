// Copyright 2024, Linaro Limited
// Author(s): Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
// SPDX-License-Identifier: GPL-2.0-or-later

use proc_macro::TokenStream;
use quote::{format_ident, quote, ToTokens};
use syn::{
    parse::{Parse, ParseStream},
    Result,
};
use syn::{parse_macro_input, DeriveInput};

use crate::{symbols::*, utilities::*, vmstate};

#[derive(Debug, Default)]
struct DeriveContainer {
    category: Option<syn::Path>,
    vmstate_fields: Option<syn::Expr>,
    vmstate_subsections: Option<syn::Expr>,
    class_name: Option<syn::Ident>,
    class_name_override: Option<syn::Ident>,
}

impl Parse for DeriveContainer {
    fn parse(input: ParseStream) -> Result<Self> {
        let _: syn::Token![#] = input.parse()?;
        let bracketed;
        _ = syn::bracketed!(bracketed in input);
        assert_eq!(DEVICE, bracketed.parse::<syn::Ident>()?);
        let mut retval = Self {
            category: None,
            vmstate_fields: None,
            vmstate_subsections: None,
            class_name: None,
            class_name_override: None,
        };
        let content;
        _ = syn::parenthesized!(content in bracketed);
        while !content.is_empty() {
            let value: syn::Ident = content.parse()?;
            if value == CLASS_NAME {
                let _: syn::Token![=] = content.parse()?;
                if retval.class_name.is_some() {
                    panic!("{} can only be used at most once", CLASS_NAME);
                }
                retval.class_name = Some(content.parse()?);
            } else if value == CLASS_NAME_OVERRIDE {
                let _: syn::Token![=] = content.parse()?;
                if retval.class_name_override.is_some() {
                    panic!("{} can only be used at most once", CLASS_NAME_OVERRIDE);
                }
                retval.class_name_override = Some(content.parse()?);
            } else if value == CATEGORY {
                let _: syn::Token![=] = content.parse()?;
                if retval.category.is_some() {
                    panic!("{} can only be used at most once", CATEGORY);
                }
                let lit: syn::LitStr = content.parse()?;
                let path: syn::Path = lit.parse()?;
                retval.category = Some(path);
            } else if value == VMSTATE_FIELDS {
                let _: syn::Token![=] = content.parse()?;
                if retval.vmstate_fields.is_some() {
                    panic!("{} can only be used at most once", VMSTATE_FIELDS);
                }
                let expr: syn::Expr = content.parse()?;
                retval.vmstate_fields = Some(expr);
            } else if value == VMSTATE_SUBSECTIONS {
                let _: syn::Token![=] = content.parse()?;
                if retval.vmstate_subsections.is_some() {
                    panic!("{} can only be used at most once", VMSTATE_SUBSECTIONS);
                }
                let expr: syn::Expr = content.parse()?;
                retval.vmstate_subsections = Some(expr);
            } else {
                panic!("unrecognized token `{}`", value);
            }

            if !content.is_empty() {
                let _: syn::Token![,] = content.parse()?;
            }
        }
        if retval.class_name.is_some() && retval.class_name_override.is_some() {
            panic!(
                "Cannot define `{}` and `{}` at the same time",
                CLASS_NAME, CLASS_NAME_OVERRIDE
            );
        }
        Ok(retval)
    }
}

#[derive(Debug)]
struct QdevProperty {
    name: Option<syn::LitCStr>,
    qdev_prop: Option<syn::Path>,
}

impl Parse for QdevProperty {
    fn parse(input: ParseStream) -> Result<Self> {
        let _: syn::Token![#] = input.parse()?;
        let bracketed;
        _ = syn::bracketed!(bracketed in input);
        assert_eq!(PROPERTY, bracketed.parse::<syn::Ident>()?);
        let mut retval = Self {
            name: None,
            qdev_prop: None,
        };
        let content;
        _ = syn::parenthesized!(content in bracketed);
        while !content.is_empty() {
            let value: syn::Ident = content.parse()?;
            if value == NAME {
                let _: syn::Token![=] = content.parse()?;
                if retval.name.is_some() {
                    panic!("{} can only be used at most once", NAME);
                }
                retval.name = Some(content.parse()?);
            } else if value == QDEV_PROP {
                let _: syn::Token![=] = content.parse()?;
                if retval.qdev_prop.is_some() {
                    panic!("{} can only be used at most once", QDEV_PROP);
                }
                retval.qdev_prop = Some(content.parse()?);
            } else {
                panic!("unrecognized token `{}`", value);
            }

            if !content.is_empty() {
                let _: syn::Token![,] = content.parse()?;
            }
        }
        Ok(retval)
    }
}

pub fn derive_device(input: TokenStream) -> TokenStream {
    let input = parse_macro_input!(input as DeriveInput);

    assert_is_repr_c_struct(&input, "Device");

    let derive_container: DeriveContainer = input
        .attrs
        .iter()
        .find(|a| a.path() == DEVICE)
        .map(|a| syn::parse(a.to_token_stream().into()).expect("could not parse device attr"))
        .unwrap_or_default();
    let (qdev_properties_static, qdev_properties_expanded) = make_qdev_properties(&input);
    let class_expanded = gen_device_class(derive_container, qdev_properties_static, &input.ident);
    let name = input.ident;

    let realize_fn = format_ident!("__{}_realize_generated", name);
    let reset_fn = format_ident!("__{}_reset_generated", name);

    let expanded = quote! {
        unsafe impl ::qemu_api::objects::DeviceImplUnsafe for #name {
            const REALIZE: ::core::option::Option<
                unsafe extern "C" fn(
                    dev: *mut ::qemu_api::bindings::DeviceState,
                    errp: *mut *mut ::qemu_api::bindings::Error,
                ),
                > = Some(#realize_fn);
            const RESET: ::core::option::Option<
                unsafe extern "C" fn(dev: *mut ::qemu_api::bindings::DeviceState),
                > = Some(#reset_fn);
        }

        #[no_mangle]
        pub unsafe extern "C" fn #realize_fn(
            dev: *mut ::qemu_api::bindings::DeviceState,
            errp: *mut *mut ::qemu_api::bindings::Error,
        ) {
            let mut instance = NonNull::new(dev.cast::<#name>()).expect(concat!("Expected dev to be a non-null pointer of type ", stringify!(#name)));
            unsafe {
                ::qemu_api::objects::DeviceImpl::realize(instance.as_mut());
            }
        }

        #[no_mangle]
        pub unsafe extern "C" fn #reset_fn(
            dev: *mut ::qemu_api::bindings::DeviceState,
        ) {
            let mut instance = NonNull::new(dev.cast::<#name>()).expect(concat!("Expected dev to be a non-null pointer of type ", stringify!(#name)));
            unsafe {
                ::qemu_api::objects::DeviceImpl::reset(instance.as_mut());
            }
        }

        #qdev_properties_expanded
        #class_expanded
    };

    TokenStream::from(expanded)
}

fn make_qdev_properties(input: &DeriveInput) -> (syn::Ident, proc_macro2::TokenStream) {
    let name = &input.ident;

    let qdev_properties: Vec<(syn::Field, QdevProperty)> = match &input.data {
        syn::Data::Struct(syn::DataStruct {
            fields: syn::Fields::Named(fields),
            ..
        }) => fields
            .named
            .iter()
            .map(|f| {
                f.attrs
                    .iter()
                    .filter(|a| a.path() == PROPERTY)
                    .map(|a| (f.clone(), a.clone()))
            })
            .flatten()
            .map(|(f, a)| {
                (
                    f.clone(),
                    syn::parse(a.to_token_stream().into()).expect("could not parse property attr"),
                )
            })
            .collect::<Vec<(syn::Field, QdevProperty)>>(),
        _other => unreachable!(),
    };

    let mut properties_expanded = quote! {
        unsafe { ::core::mem::MaybeUninit::<::qemu_api::bindings::Property>::zeroed().assume_init() }
    };
    let prop_len = qdev_properties.len() + 1;
    for (field, prop) in qdev_properties {
        let prop_name = prop.name.as_ref().unwrap();
        let field_name = field.ident.as_ref().unwrap();
        let qdev_prop = prop.qdev_prop.as_ref().unwrap();
        let prop = quote! {
            ::qemu_api::bindings::Property {
                name: ::core::ffi::CStr::as_ptr(#prop_name),
                info: unsafe { &#qdev_prop },
                offset: ::core::mem::offset_of!(#name, #field_name) as _,
                bitnr: 0,
                bitmask: 0,
                set_default: false,
                defval: ::qemu_api::bindings::Property__bindgen_ty_1 { i: 0 },
                arrayoffset: 0,
                arrayinfo: ::core::ptr::null(),
                arrayfieldsize: 0,
                link_type: ::core::ptr::null(),
            }
        };
        properties_expanded = quote! {
            #prop,
            #properties_expanded
        };
    }
    let properties_ident = format_ident!("__{}_QDEV_PROPERTIES", name);
    let expanded = quote! {
        #[no_mangle]
        pub static mut #properties_ident: [::qemu_api::bindings::Property; #prop_len] = [#properties_expanded];
    };
    (properties_ident, expanded)
}

fn gen_device_class(
    derive_container: DeriveContainer,
    qdev_properties_static: syn::Ident,
    name: &syn::Ident,
) -> proc_macro2::TokenStream {
    let (class_name, class_def) = match (
        derive_container.class_name_override,
        derive_container.class_name,
    ) {
        (Some(class_name), _) => {
            let class_expanded = quote! {
                #[repr(C)]
                pub struct #class_name {
                    _inner: [u8; 0],
                }
            };
            (class_name, class_expanded)
        }
        (None, Some(class_name)) => (class_name, quote! {}),
        (None, None) => {
            let class_name = format_ident!("{}Class", name);
            let class_expanded = quote! {
                #[repr(C)]
                pub struct #class_name {
                    _inner: [u8; 0],
                }
            };
            (class_name, class_expanded)
        }
    };
    let class_init_fn = format_ident!("__{}_class_init_generated", class_name);
    let class_base_init_fn = format_ident!("__{}_class_base_init_generated", class_name);

    let (vmsd, vmsd_impl) = {
        let (i, vmsd) = vmstate::make_vmstate(
            name,
            derive_container.vmstate_fields,
            derive_container.vmstate_subsections,
        );
        (quote! { &#i }, vmsd)
    };
    let category = if let Some(category) = derive_container.category {
        quote! {
            const BITS_PER_LONG: u32 = ::core::ffi::c_ulong::BITS;
            let _: ::qemu_api::bindings::DeviceCategory = #category;
            let nr: ::core::ffi::c_ulong = #category as _;
            let mask = 1 << (nr as u32 % BITS_PER_LONG);
            let p = ::core::ptr::addr_of_mut!(dc.as_mut().categories).offset((nr as u32 / BITS_PER_LONG) as isize);
            let p: *mut ::core::ffi::c_ulong = p.cast();
            let categories = p.read_unaligned();
            p.write_unaligned(categories | mask);
        }
    } else {
        quote! {}
    };
    let props = quote! {
        ::qemu_api::bindings::device_class_set_props(dc.as_mut(), #qdev_properties_static.as_mut_ptr());
    };

    quote! {
        #class_def

        impl ::qemu_api::objects::ClassImpl for #class_name {
            type Object = #name;
        }

        unsafe impl ::qemu_api::objects::ClassImplUnsafe for #class_name {
            const CLASS_INIT: Option<
                unsafe extern "C" fn(klass: *mut ObjectClass, data: *mut core::ffi::c_void),
                > = Some(#class_init_fn);
            const CLASS_BASE_INIT: Option<
                unsafe extern "C" fn(klass: *mut ObjectClass, data: *mut core::ffi::c_void),
                > = Some(#class_base_init_fn);
        }

        #[no_mangle]
        pub unsafe extern "C" fn #class_init_fn(klass: *mut ObjectClass, data: *mut core::ffi::c_void) {
            {
                {
                    let mut dc =
                        ::core::ptr::NonNull::new(klass.cast::<::qemu_api::bindings::DeviceClass>()).unwrap();
                    unsafe {
                        dc.as_mut().realize =
                            <#name as ::qemu_api::objects::DeviceImplUnsafe>::REALIZE;
                        ::qemu_api::bindings::device_class_set_legacy_reset(
                            dc.as_mut(),
                            <#name as ::qemu_api::objects::DeviceImplUnsafe>::RESET
                        );
                        dc.as_mut().vmsd = #vmsd;
                        #props
                        #category
                    }
                }
                let mut klass = NonNull::new(klass.cast::<#class_name>()).expect(concat!("Expected klass to be a non-null pointer of type ", stringify!(#class_name)));
                unsafe {
                    ::qemu_api::objects::ClassImpl::class_init(klass.as_mut(), data);
                }
            }
        }
        #[no_mangle]
        pub unsafe extern "C" fn #class_base_init_fn(klass: *mut ObjectClass, data: *mut core::ffi::c_void) {
            {
                let mut klass = NonNull::new(klass.cast::<#class_name>()).expect(concat!("Expected klass to be a non-null pointer of type ", stringify!(#class_name)));
                unsafe {
                    ::qemu_api::objects::ClassImpl::class_base_init(klass.as_mut(), data);
                }
            }
        }

        #vmsd_impl
    }
}
