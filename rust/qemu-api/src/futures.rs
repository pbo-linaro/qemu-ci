use crate::bindings;
use std::ffi::c_void;
use std::future::Future;
use std::mem::MaybeUninit;
use std::sync::Arc;
use std::task::{Context, Poll, Wake};

struct RunFutureWaker {
    co: *mut bindings::Coroutine,
}
unsafe impl Send for RunFutureWaker {}
unsafe impl Sync for RunFutureWaker {}

impl Wake for RunFutureWaker {
    fn wake(self: Arc<Self>) {
        unsafe {
            bindings::aio_co_wake(self.co);
        }
    }
}

/// Use QEMU's event loops to run a Rust [`Future`] to completion and return its result.
///
/// This function must be called in coroutine context. If the future isn't ready yet, it yields.
pub fn qemu_co_run_future<F: Future>(future: F) -> F::Output {
    let waker = Arc::new(RunFutureWaker {
        co: unsafe { bindings::qemu_coroutine_self() },
    })
    .into();
    let mut cx = Context::from_waker(&waker);

    let mut pinned_future = std::pin::pin!(future);
    loop {
        match pinned_future.as_mut().poll(&mut cx) {
            Poll::Ready(res) => return res,
            Poll::Pending => unsafe {
                bindings::qemu_coroutine_yield();
            },
        }
    }
}

/// Wrapper around [`qemu_co_run_future`] that can be called from C.
///
/// # Safety
///
/// `future` must be a valid pointer to an owned `F` (it will be freed in this function).  `output`
/// must be a valid pointer representing a mutable reference to an `F::Output` where the result can
/// be stored.
unsafe extern "C" fn rust_co_run_future<F: Future>(
    future: *mut bindings::RustBoxedFuture,
    output: *mut c_void,
) {
    let future = unsafe { Box::from_raw(future.cast::<F>()) };
    let output = output.cast::<F::Output>();
    let ret = qemu_co_run_future(*future);
    unsafe {
        *output = ret;
    }
}

/// Use QEMU's event loops to run a Rust [`Future`] to completion and return its result.
///
/// This function must be called outside of coroutine context to avoid deadlocks. It blocks and
/// runs a nested even loop until the future is ready and returns a result.
pub fn qemu_run_future<F: Future>(future: F) -> F::Output {
    let future_ptr = Box::into_raw(Box::new(future));
    let mut output = MaybeUninit::<F::Output>::uninit();
    unsafe {
        bindings::rust_run_future(
            future_ptr.cast::<bindings::RustBoxedFuture>(),
            #[allow(clippy::as_underscore)]
            Some(rust_co_run_future::<F> as _),
            output.as_mut_ptr().cast::<c_void>(),
        );
        output.assume_init()
    }
}
