// Copyright Red Hat Inc.
// Author(s): Kevin Wolf <kwolf@redhat.com>
// SPDX-License-Identifier: GPL-2.0-or-later

use std::mem::MaybeUninit;

/// Types that implement IoBuffer can be used with safe I/O functions.
///
/// # Safety
///
/// `buffer_ptr()` and `buffer_mut_ptr()` must return pointers to the address of the same I/O
/// buffer with the size returned by `buffer_len()` which remain valid for the lifetime of the
/// object. It must be safe for the I/O buffer to contain any byte patterns.
pub unsafe trait IoBuffer {
    /// Returns a const pointer to be used as a raw I/O buffer
    fn buffer_ptr(&self) -> *const u8;

    /// Returns a mutable pointer to be used as a raw I/O buffer
    fn buffer_mut_ptr(&mut self) -> *mut u8;

    /// Returns the length in bytes for the raw I/O buffer returned by [`buffer_ptr`] and
    /// [`buffer_mut_ptr`]
    ///
    /// [`buffer_ptr`]: IoBuffer::buffer_ptr
    /// [`buffer_mut_ptr`]: IoBuffer::buffer_mut_ptr
    fn buffer_len(&self) -> usize;
}

/// Implementing `SizedIoBuffer` provides an implementation for [`IoBuffer`] without having to
/// implement any functions manually.
///
/// # Safety
///
/// Types implementing `SizedIoBuffer` guarantee that the whole object can be accessed as an I/O
/// buffer that is safe to contain any byte patterns.
pub unsafe trait SizedIoBuffer: Sized {
    /// Safely converts a byte slice into a shared reference to the type implementing
    /// `SizedIoBuffer`
    fn from_byte_slice(buf: &[u8]) -> Option<&Self> {
        if buf.len() < std::mem::size_of::<Self>() {
            return None;
        }

        let ptr = buf.as_ptr() as *const Self;

        // TODO Use ptr.is_aligned() when MSRV is updated to at least 1.79.0
        if (ptr as usize) % std::mem::align_of::<Self>() != 0 {
            return None;
        }

        // SAFETY: This function checked that the byte slice is large enough and aligned.
        // Implementing SizedIoBuffer promises that any byte pattern is valid for the type.
        Some(unsafe { &*ptr })
    }
}

unsafe impl<T: SizedIoBuffer> IoBuffer for T {
    fn buffer_ptr(&self) -> *const u8 {
        self as *const Self as *const u8
    }

    fn buffer_mut_ptr(&mut self) -> *mut u8 {
        self as *mut Self as *mut u8
    }

    fn buffer_len(&self) -> usize {
        std::mem::size_of::<Self>()
    }
}

unsafe impl<T: SizedIoBuffer> IoBuffer for [T] {
    fn buffer_ptr(&self) -> *const u8 {
        self.as_ptr() as *const u8
    }

    fn buffer_mut_ptr(&mut self) -> *mut u8 {
        self.as_mut_ptr() as *mut u8
    }

    fn buffer_len(&self) -> usize {
        std::mem::size_of_val(self)
    }
}

unsafe impl<T: SizedIoBuffer> SizedIoBuffer for MaybeUninit<T> {}

unsafe impl SizedIoBuffer for u8 {}
unsafe impl SizedIoBuffer for u16 {}
unsafe impl SizedIoBuffer for u32 {}
unsafe impl SizedIoBuffer for u64 {}
unsafe impl SizedIoBuffer for i8 {}
unsafe impl SizedIoBuffer for i16 {}
unsafe impl SizedIoBuffer for i32 {}
unsafe impl SizedIoBuffer for i64 {}
