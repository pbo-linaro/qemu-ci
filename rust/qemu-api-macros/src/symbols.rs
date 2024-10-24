// Copyright 2024, Linaro Limited
// Author(s): Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
// SPDX-License-Identifier: GPL-2.0-or-later

use core::fmt;
use syn::{Ident, Path};

#[derive(Copy, Clone, Debug)]
pub struct Symbol(&'static str);

pub const DEVICE: Symbol = Symbol("device");
pub const NAME: Symbol = Symbol("name");
pub const CATEGORY: Symbol = Symbol("category");
pub const CLASS_NAME: Symbol = Symbol("class_name");
pub const CLASS_NAME_OVERRIDE: Symbol = Symbol("class_name_override");
pub const QDEV_PROP: Symbol = Symbol("qdev_prop");
pub const MIGRATEABLE: Symbol = Symbol("migrateable");
pub const PROPERTIES: Symbol = Symbol("properties");
pub const PROPERTY: Symbol = Symbol("property");

impl PartialEq<Symbol> for Ident {
    fn eq(&self, word: &Symbol) -> bool {
        self == word.0
    }
}

impl<'a> PartialEq<Symbol> for &'a Ident {
    fn eq(&self, word: &Symbol) -> bool {
        *self == word.0
    }
}

impl PartialEq<Symbol> for Path {
    fn eq(&self, word: &Symbol) -> bool {
        self.is_ident(word.0)
    }
}

impl<'a> PartialEq<Symbol> for &'a Path {
    fn eq(&self, word: &Symbol) -> bool {
        self.is_ident(word.0)
    }
}

impl PartialEq<Ident> for Symbol {
    fn eq(&self, ident: &Ident) -> bool {
        ident == self.0
    }
}

impl fmt::Display for Symbol {
    fn fmt(&self, fmt: &mut fmt::Formatter) -> fmt::Result {
        self.0.fmt(fmt)
    }
}
