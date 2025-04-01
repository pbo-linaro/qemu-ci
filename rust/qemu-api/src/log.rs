#[allow(non_camel_case_types)]
#[repr(u32)]
pub enum Mask {
    cpu_log_tb_out_asm = crate::bindings::CPU_LOG_TB_OUT_ASM,
    cpu_log_tb_in_asm = crate::bindings::CPU_LOG_TB_IN_ASM,
    cpu_log_tb_op = crate::bindings::CPU_LOG_TB_OP,
    cpu_log_tb_op_opt = crate::bindings::CPU_LOG_TB_OP_OPT,
    cpu_log_int = crate::bindings::CPU_LOG_INT,
    cpu_log_exec = crate::bindings::CPU_LOG_EXEC,
    cpu_log_pcall = crate::bindings::CPU_LOG_PCALL,
    cpu_log_tb_cpu = crate::bindings::CPU_LOG_TB_CPU,
    cpu_log_reset = crate::bindings::CPU_LOG_RESET,
    log_unimp = crate::bindings::LOG_UNIMP,
    log_guest_error = crate::bindings::LOG_GUEST_ERROR,
    cpu_log_mmu = crate::bindings::CPU_LOG_MMU,
    cpu_log_tb_nochain = crate::bindings::CPU_LOG_TB_NOCHAIN,
    cpu_log_page = crate::bindings::CPU_LOG_PAGE,
    cpu_log_tb_op_ind = crate::bindings::CPU_LOG_TB_OP_IND,
    cpu_log_tb_fpu = crate::bindings::CPU_LOG_TB_FPU,
    cpu_log_plugin = crate::bindings::CPU_LOG_PLUGIN,
    log_strace = crate::bindings::LOG_STRACE,
    log_per_thread = crate::bindings::LOG_PER_THREAD,
    cpu_log_tb_vpu = crate::bindings::CPU_LOG_TB_VPU,
    log_tb_op_plugin = crate::bindings::LOG_TB_OP_PLUGIN,
    log_invalid_mem = crate::bindings::LOG_INVALID_MEM,
}

#[macro_export]
macro_rules! qemu_log_mask {
    ($mask:expr, $fmt:expr $(, $arg:expr)*) => {{
        let mask: Mask = $mask;
        unsafe {
            if $crate::bindings::qemu_loglevel_mask(mask as std::os::raw::c_int) {
                let format_str = std::ffi::CString::new($fmt).expect("CString::new failed");
                $crate::bindings::qemu_log(format_str.as_ptr() $(, $arg)*);
            }
        }
    }};
}

#[macro_export]
macro_rules! qemu_log_mask_and_addr {
    ($mask:expr, $addr:expr, $fmt:expr $(, $arg:expr)*) => {{
        let mask: Mask = $mask;
        let addr: $crate::bindings::hwaddr = $addr;
        unsafe {
            if $crate::bindings::qemu_loglevel_mask(mask as std::os::raw::c_int) &&
                $crate::bindings::qemu_log_in_addr_range(addr) {
                let format_str = std::ffi::CString::new($fmt).expect("CString::new failed");
                $crate::bindings::qemu_log(format_str.as_ptr() $(, $arg)*);
            }
        }
    }};
}
