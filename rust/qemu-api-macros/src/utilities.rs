// Copyright 2024, Linaro Limited
// Author(s): Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
// SPDX-License-Identifier: GPL-2.0-or-later

use syn::{parenthesized, token, Data, DeriveInput, LitInt};

#[derive(Default)]
pub enum Abi {
    #[default]
    Rust,
    C,
    Transparent,
    Other(String),
}

#[derive(Default)]
pub struct Repr {
    pub abi: Abi,
    /// whether the attribute was declared in the definition.
    pub present: bool,
    pub align: Option<usize>,
    pub packed: Option<usize>,
}

impl core::fmt::Display for Repr {
    fn fmt(&self, fmt: &mut core::fmt::Formatter) -> core::fmt::Result {
        write!(fmt, "repr(")?;
        match &self.abi {
            Abi::C => write!(fmt, "C")?,
            Abi::Rust => write!(fmt, "Rust")?,
            Abi::Transparent => write!(fmt, "transparent")?,
            Abi::Other(s) => write!(fmt, "{}", s)?,
        }
        if self.align.is_some() || self.packed.is_some() {
            write!(fmt, ", ")?;
            if let Some(v) = self.align {
                write!(fmt, "align({})", v)?;
                if self.packed.is_some() {
                    write!(fmt, ", ")?;
                }
            }
            match self.packed {
                Some(1) => write!(fmt, "packed")?,
                Some(n) => write!(fmt, "packed({})", n)?,
                None => {}
            }
        }
        write!(fmt, ")")
    }
}

impl Repr {
    pub fn detect_repr(attrs: &[syn::Attribute]) -> Self {
        let mut repr = Self::default();

        // We don't validate the repr attribute; if it's invalid rustc will complain
        // anyway.
        for attr in attrs {
            if attr.path().is_ident("repr") {
                repr.present = true;
                if let Err(err) = attr.parse_nested_meta(|meta| {
                    // #[repr(C)]
                    if meta.path.is_ident("C") {
                        repr.abi = Abi::C;
                        return Ok(());
                    }

                    // #[repr(Rust)]
                    if meta.path.is_ident("Rust") {
                        repr.abi = Abi::Rust;
                        return Ok(());
                    }

                    // #[repr(transparent)]
                    if meta.path.is_ident("transparent") {
                        repr.abi = Abi::Transparent;
                        return Ok(());
                    }

                    // #[repr(align(N))]
                    if meta.path.is_ident("align") {
                        let content;
                        parenthesized!(content in meta.input);
                        let lit: LitInt = content.parse()?;
                        let n: usize = lit.base10_parse()?;
                        repr.align = Some(n);
                        return Ok(());
                    }

                    // #[repr(packed)] or #[repr(packed(N))], omitted N means 1
                    if meta.path.is_ident("packed") {
                        repr.packed = if meta.input.peek(token::Paren) {
                            let content;
                            parenthesized!(content in meta.input);
                            let lit: LitInt = content.parse()?;
                            let n: usize = lit.base10_parse()?;
                            Some(n)
                        } else {
                            Some(1)
                        };
                        return Ok(());
                    }

                    if let Some(i) = meta.path.get_ident() {
                        repr.abi = Abi::Other(i.to_string());
                    }

                    Err(meta.error("unrecognized repr"))
                }) {
                    println!("Error while processing Object Derive macro: {}", err);
                }
            }
        }
        repr
    }
}

pub fn assert_is_repr_c_struct(input: &DeriveInput, derive_macro: &'static str) {
    if !matches!(input.data, Data::Struct(_)) {
        panic!(
            "`{}` derive macro can only be used with structs, and `{}` is {}",
            derive_macro,
            input.ident,
            match input.data {
                Data::Struct(_) => unreachable!(),
                Data::Enum(_) => "enum",
                Data::Union(_) => "union",
            }
        );
    }
    match Repr::detect_repr(&input.attrs) {
        Repr { abi: Abi::C, .. } => { /* all good */ }
        Repr {
            abi: Abi::Transparent,
            ..
        } => {
            // If the data layout is `transparent`, then its representation
            // depends on the ABI of the wrapped type. We cannot
            // detect it here.
        }
        other => {
            panic!(
                "`{}` derive macro can only be used with repr(C) structs, and `{}` {} \
                 {}\nHint: Annotate the struct with `#[repr(C)]`.",
                derive_macro,
                input.ident,
                if other.present { "is" } else { "defaults to" },
                other,
            );
        }
    }
}
