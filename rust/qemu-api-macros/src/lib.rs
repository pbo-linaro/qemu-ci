// Copyright 2024, Linaro Limited
// Author(s): Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
// SPDX-License-Identifier: GPL-2.0-or-later

#![allow(dead_code)]

use proc_macro::TokenStream;

mod device;
mod object;
mod symbols;
mod utilities;

#[proc_macro_derive(Object)]
pub fn derive_object(input: TokenStream) -> TokenStream {
    object::derive_object(input)
}

#[proc_macro_derive(Device, attributes(device, property))]
pub fn derive_device(input: TokenStream) -> TokenStream {
    device::derive_device(input)
}
