_block_rs = static_library(
  'block',
  files('src/lib.rs'),
  override_options: ['rust_std=2021', 'build.rust_std=2021'],
  rust_abi: 'rust',
  dependencies: [
    qemu_api_tools,
    qemu_api_macros,
  ],
  rust_dependency_map: {'qemu_api_tools': 'qemu_api'},
)

rust_block_ss.add(if_true: [declare_dependency(
  link_whole: [_block_rs],
  # Putting proc macro crates in `dependencies` is necessary for Meson to find
  # them when compiling the root per-target static rust lib.
  dependencies: [qemu_api_macros],
  variables: {'crate': 'block'},
)])
