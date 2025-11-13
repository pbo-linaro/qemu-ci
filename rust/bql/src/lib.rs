// SPDX-License-Identifier: GPL-2.0-or-later

mod bindings;
use bindings::{bql_block_unlock, bql_locked, rust_bql_lock, rust_bql_mock_lock, rust_bql_unlock};

mod cell;
pub use cell::*;

/// An internal function that is used by doctests.
pub fn start_test() {
    // SAFETY: integration tests are run with --test-threads=1, while
    // unit tests and doctests are not multithreaded and do not have
    // any BQL-protected data.  Just set bql_locked to true.
    unsafe {
        rust_bql_mock_lock();
    }
}

pub fn is_locked() -> bool {
    // SAFETY: the function does nothing but return a thread-local bool
    unsafe { bql_locked() }
}

pub fn block_unlock(increase: bool) {
    // SAFETY: this only adjusts a counter
    unsafe {
        bql_block_unlock(increase);
    }
}

// this function is private since user should get BQL context via BqlGuard.
fn lock() {
    // SAFETY: the function locks bql which lifetime is enough to cover
    // the device's entire lifetime.
    unsafe {
        rust_bql_lock();
    }
}

// this function is private since user should get BQL context via BqlGuard.
fn unlock() {
    // SAFETY: the function unlocks bql which lifetime is enough to
    // cover the device's entire lifetime.
    unsafe {
        rust_bql_unlock();
    }
}

/// An RAII guard to ensure a block of code runs within the BQL context.
///
/// It checks if the BQL is already locked at its creation:
/// * If not, it locks the BQL and will release it when it is dropped.
/// * If yes, it blocks BQL unlocking until its lifetime is end.
#[must_use]
pub struct BqlGuard {
    locked: bool,
}

impl BqlGuard {
    /// Creates a new `BqlGuard` to ensure BQL is locked during its
    /// lifetime.
    ///
    /// # Examples
    ///
    /// ```
    /// use bql::{BqlCell, BqlGuard};
    ///
    /// fn foo() {
    ///     let _guard = BqlGuard::new(); // BQL is locked
    ///
    ///     let c = BqlCell::new(5);
    ///     assert_eq!(c.get(), 5);
    /// } // BQL could be unlocked
    /// ```
    pub fn new() -> Self {
        if !is_locked() {
            lock();
            Self { locked: true }
        } else {
            block_unlock(true);
            Self { locked: false }
        }
    }
}

impl Default for BqlGuard {
    fn default() -> Self {
        Self::new()
    }
}

impl Drop for BqlGuard {
    fn drop(&mut self) {
        if self.locked {
            unlock();
        } else {
            block_unlock(false);
        }
    }
}

/// Executes a closure (function) within the BQL context.
///
/// This function creates a `BqlGuard`.
///
/// # Examples
///
/// ```should_panic
/// use bql::{with_guard, BqlRefCell};
///
/// let c = BqlRefCell::new(5);
///
/// with_guard(|| {
///     // BQL is locked
///     let m = c.borrow();
///
///     assert_eq!(*m, 5);
/// }); // BQL could be unlocked
///
/// let b = c.borrow(); // this causes a panic
/// ```
pub fn with_guard<F, R>(f: F) -> R
where
    F: FnOnce() -> R,
{
    let _guard = BqlGuard::new();
    f()
}
