_qemu_api_cfg = ['--cfg', 'MESON']
# _qemu_api_cfg += ['--cfg', 'feature="allocator"']
if rustc.version().version_compare('>=1.77.0')
  _qemu_api_cfg += ['--cfg', 'has_offset_of']
endif

_qemu_api_rs = static_library(
  'qemu_api',
  structured_sources(
    [
      'src/lib.rs',
      'src/c_str.rs',
      'src/definitions.rs',
      'src/device_class.rs',
      'src/offset_of.rs',
      'src/tests.rs',
    ],
    {'.' : bindings_rs},
  ),
  override_options: ['rust_std=2021', 'build.rust_std=2021'],
  rust_abi: 'rust',
  rust_args: _qemu_api_cfg,
  dependencies: [
    qemu_api_macros,
  ],
)

rust.test('rust-qemu-api-tests', _qemu_api_rs)

qemu_api = declare_dependency(
  link_with: _qemu_api_rs,
)
