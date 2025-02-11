// Copyright Red Hat Inc.
// Author(s): Kevin Wolf <kwolf@redhat.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// All of this is unused until the first block driver is added
#![allow(dead_code)]
#![allow(unused_macros)]
#![allow(unused_imports)]

use crate::{IoBuffer, SizedIoBuffer};
use qemu_api::bindings;
use qemu_api::futures::qemu_co_run_future;
use std::cmp::min;
use std::ffi::c_void;
use std::io::{self, Error, ErrorKind};
use std::mem::MaybeUninit;
use std::ptr;

/// A request to a block driver
pub enum Request {
    Read { offset: u64, len: u64 },
}

/// The target for a number of guest blocks, e.g. a location in a child node or the information
/// that the described blocks are unmapped.
// FIXME Actually use node
#[allow(dead_code)]
pub enum MappingTarget {
    /// The described blocks are unallocated. Reading from them yields zeros.
    Unmapped,

    /// The described blocks are stored in a child node.
    Data {
        /// Child node in which the data is stored
        node: (),

        /// Offset in the child node at which the data is stored
        offset: u64,
    },
}

/// A mapping for a number of contiguous guest blocks
pub struct Mapping {
    /// Offset of the mapped blocks from the perspective of the guest
    pub offset: u64,
    /// Length of the mapping in bytes
    pub len: u64,
    /// Where the data for the described blocks is stored
    pub target: MappingTarget,
}

/// A trait for writing block drivers.
///
/// Types that implement this trait can be registered as QEMU block drivers using the
/// [`block_driver`] macro.
pub trait BlockDriver {
    /// The type that contains the block driver specific options for opening an image
    type Options;

    // TODO Native support for QAPI types and deserialization
    unsafe fn parse_options(
        v: &mut bindings::Visitor,
        opts: &mut *mut Self::Options,
        errp: *mut *mut bindings::Error,
    );
    unsafe fn free_options(opts: *mut Self::Options);
    unsafe fn open(
        bs: *mut bindings::BlockDriverState,
        opts: &Self::Options,
        errp: *mut *mut bindings::Error,
    ) -> std::os::raw::c_int;

    /// Returns the size of the image in bytes
    fn size(&self) -> u64;

    /// Returns the mapping for the first part of `req`. If the returned mapping is shorter than
    /// the request, the function can be called again with a shortened request to get the mapping
    /// for the remaining part.
    async fn map(&self, req: &Request) -> io::Result<Mapping>;
}

/// Represents the connection between a parent and its child node.
///
/// This is a wrapper around the `BdrvChild` type in C.
pub struct BdrvChild {
    child: *mut bindings::BdrvChild,
}

impl BdrvChild {
    /// Creates a new child reference from a `BlockdevRef`.
    pub unsafe fn new(
        parent: *mut bindings::BlockDriverState,
        bref: *mut bindings::BlockdevRef,
        errp: *mut *mut bindings::Error,
    ) -> Option<Self> {
        unsafe {
            let child_bs = bindings::bdrv_open_blockdev_ref_file(bref, parent, errp);
            if child_bs.is_null() {
                return None;
            }

            bindings::bdrv_graph_wrlock();
            let child = bindings::bdrv_attach_child(
                parent,
                child_bs,
                c"file".as_ptr(),
                &bindings::child_of_bds as *const _,
                bindings::BDRV_CHILD_IMAGE,
                errp,
            );
            bindings::bdrv_graph_wrunlock();

            if child.is_null() {
                None
            } else {
                Some(BdrvChild { child })
            }
        }
    }

    /// Reads data from the child node into a linear byte buffer.
    ///
    /// # Safety
    ///
    /// `buf` must be a valid I/O buffer that can store at least `bytes` bytes.
    pub async unsafe fn read_raw(&self, offset: u64, bytes: usize, buf: *mut u8) -> io::Result<()> {
        let offset: i64 = offset
            .try_into()
            .map_err(|e| Error::new(ErrorKind::InvalidInput, e))?;
        let bytes: i64 = bytes
            .try_into()
            .map_err(|e| Error::new(ErrorKind::InvalidInput, e))?;

        let ret = unsafe { bindings::bdrv_pread(self.child, offset, bytes, buf as *mut c_void, 0) };
        if ret < 0 {
            Err(Error::from_raw_os_error(ret))
        } else {
            Ok(())
        }
    }

    /// Reads data from the child node into a linear typed buffer.
    pub async fn read<T: IoBuffer + ?Sized>(&self, offset: u64, buf: &mut T) -> io::Result<()> {
        unsafe {
            self.read_raw(offset, buf.buffer_len(), buf.buffer_mut_ptr())
                .await
        }
    }

    /// Reads data from the child node into a linear, potentially uninitialised typed buffer.
    pub async fn read_uninit<T: SizedIoBuffer>(
        &self,
        offset: u64,
        mut buf: MaybeUninit<T>,
    ) -> io::Result<T> {
        unsafe {
            self.read_raw(offset, buf.buffer_len(), buf.buffer_mut_ptr())
                .await?;
            Ok(buf.assume_init())
        }
    }
}

#[doc(hidden)]
pub unsafe extern "C" fn bdrv_open<D: BlockDriver>(
    bs: *mut bindings::BlockDriverState,
    options: *mut bindings::QDict,
    _flags: std::os::raw::c_int,
    errp: *mut *mut bindings::Error,
) -> std::os::raw::c_int {
    unsafe {
        let v = match bindings::qobject_input_visitor_new_flat_confused(options, errp).as_mut() {
            None => return -(bindings::EINVAL as std::os::raw::c_int),
            Some(v) => v,
        };

        let mut opts: *mut D::Options = ptr::null_mut();
        D::parse_options(v, &mut opts, errp);
        bindings::visit_free(v);

        let opts = match opts.as_mut() {
            None => return -(bindings::EINVAL as std::os::raw::c_int),
            Some(opts) => opts,
        };

        while let Some(e) = bindings::qdict_first(options).as_ref() {
            bindings::qdict_del(options, e.key);
        }

        let ret = D::open(bs, opts, errp);
        D::free_options(opts);
        ret
    }
}

#[doc(hidden)]
pub unsafe extern "C" fn bdrv_close<D: BlockDriver>(bs: *mut bindings::BlockDriverState) {
    unsafe {
        let state = (*bs).opaque as *mut D;
        ptr::drop_in_place(state);
    }
}

#[doc(hidden)]
pub unsafe extern "C" fn bdrv_co_preadv_part<D: BlockDriver>(
    bs: *mut bindings::BlockDriverState,
    offset: i64,
    bytes: i64,
    qiov: *mut bindings::QEMUIOVector,
    mut qiov_offset: usize,
    flags: bindings::BdrvRequestFlags,
) -> std::os::raw::c_int {
    let s = unsafe { &mut *((*bs).opaque as *mut D) };

    let mut offset = offset as u64;
    let mut bytes = bytes as u64;

    while bytes > 0 {
        let req = Request::Read { offset, len: bytes };
        let mapping = match qemu_co_run_future(s.map(&req)) {
            Ok(mapping) => mapping,
            Err(e) => {
                return e
                    .raw_os_error()
                    .unwrap_or(-(bindings::EIO as std::os::raw::c_int))
            }
        };

        let mapping_offset = offset - mapping.offset;
        let cur_bytes = min(bytes, mapping.len - mapping_offset);

        match mapping.target {
            MappingTarget::Unmapped => unsafe {
                bindings::qemu_iovec_memset(qiov, qiov_offset, 0, cur_bytes.try_into().unwrap());
            },
            MappingTarget::Data {
                node: _,
                offset: target_offset,
            } => unsafe {
                // TODO Support using child nodes other than bs->file
                let ret = bindings::bdrv_co_preadv_part(
                    (*bs).file,
                    (target_offset + mapping_offset) as i64,
                    cur_bytes as i64,
                    qiov,
                    qiov_offset,
                    flags,
                );
                if ret < 0 {
                    return ret;
                }
            },
        }

        offset += cur_bytes;
        qiov_offset += cur_bytes as usize;
        bytes -= cur_bytes;
    }

    0
}

/// Declare a format block driver. This macro is meant to be used at the top level.
///
/// `typ` is a type implementing the [`BlockDriver`] trait to handle the image format with the
/// user-visible name `fmtname`.
macro_rules! block_driver {
    ($fmtname:expr, $typ:ty) => {
        const _: () = {
            static mut BLOCK_DRIVER: ::qemu_api::bindings::BlockDriver =
                ::qemu_api::bindings::BlockDriver {
                    format_name: ::qemu_api::c_str!($fmtname).as_ptr(),
                    instance_size: ::std::mem::size_of::<$typ>() as i32,
                    bdrv_open: Some($crate::driver::bdrv_open::<$typ>),
                    bdrv_close: Some($crate::driver::bdrv_close::<$typ>),
                    bdrv_co_preadv_part: Some($crate::driver::bdrv_co_preadv_part::<$typ>),
                    bdrv_child_perm: Some(::qemu_api::bindings::bdrv_default_perms),
                    is_format: true,
                    ..::qemu_api::zeroable::Zeroable::ZERO
                };

            qemu_api::module_init! {
                MODULE_INIT_BLOCK => unsafe {
                    ::qemu_api::bindings::bdrv_register(std::ptr::addr_of_mut!(BLOCK_DRIVER));
                }
            }
        };
    };
}
pub(crate) use block_driver;
