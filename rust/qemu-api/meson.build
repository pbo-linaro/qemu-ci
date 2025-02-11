_qemu_api_cfg = run_command(rustc_args,
  '--config-headers', config_host_h, '--features', files('Cargo.toml'),
  capture: true, check: true).stdout().strip().splitlines()

# _qemu_api_cfg += ['--cfg', 'feature="allocator"']
if rustc.version().version_compare('>=1.77.0')
  _qemu_api_cfg += ['--cfg', 'has_offset_of']
endif
if get_option('debug_mutex')
  _qemu_api_cfg += ['--cfg', 'feature="debug_cell"']
endif

sources_core = [
  'src/lib.rs',
  'src/assertions.rs',
  'src/bindings.rs',
  'src/bitops.rs',
  'src/callbacks.rs',
  'src/cell.rs',
  'src/c_str.rs',
  'src/module.rs',
  'src/offset_of.rs',
  'src/prelude.rs',
  'src/qom.rs',
  'src/zeroable.rs',
]
sources_system = sources_core + [
  'src/irq.rs',
  'src/qdev.rs',
  'src/sysbus.rs',
  'src/vmstate.rs',
]


_qemu_api_tools_rs = static_library(
  'qemu_api_tools',
  structured_sources(
    sources_core,
    {'.' : bindings_rs_tools},
  ),
  override_options: ['rust_std=2021', 'build.rust_std=2021'],
  rust_abi: 'rust',
  rust_args: _qemu_api_cfg,
)
_qemu_api_system_rs = static_library(
  'qemu_api_system',
  structured_sources(
    sources_system,
    {'.' : bindings_rs_system},
  ),
  override_options: ['rust_std=2021', 'build.rust_std=2021'],
  rust_abi: 'rust',
  rust_args: _qemu_api_cfg + ['--cfg', 'feature="system"'],
)

rust.test('rust-qemu-api-tests', _qemu_api_system_rs,
          suite: ['unit', 'rust'])

qemu_api_tools = declare_dependency(
  link_with: _qemu_api_tools_rs,
  dependencies: qemu_api_macros,
)
qemu_api_system = declare_dependency(
  link_with: _qemu_api_system_rs,
  dependencies: qemu_api_macros,
)

# Rust executables do not support objects, so add an intermediate step.
rust_qemu_api_objs = static_library(
    'rust_qemu_api_objs',
    objects: [libqom.extract_all_objects(recursive: false),
              libhwcore.extract_all_objects(recursive: false)])

test('rust-qemu-api-integration',
    executable(
        'rust-qemu-api-integration',
        'tests/tests.rs',
        override_options: ['rust_std=2021', 'build.rust_std=2021'],
        rust_args: ['--test'],
        install: false,
        dependencies: [qemu_api_system, qemu_api_macros],
        rust_dependency_map: {'qemu_api_system': 'qemu_api'},
        link_whole: [rust_qemu_api_objs, libqemuutil]),
    args: [
        '--test', '--test-threads', '1',
        '--format', 'pretty',
    ],
    protocol: 'rust',
    suite: ['unit', 'rust'])
