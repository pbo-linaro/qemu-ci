sysprof = not_found
glib_static = not_found

sysprof = dependency('sysprof-capture-4', method: 'pkg-config', required: false)
glib_static = dependency('glib-2.0', version: glib_req_ver, required: false,
                         method: 'pkg-config', static: true)


if host_os == 'linux' and sysprof.found() and glib_static.found()
    stress = executable(
      'stress',
      files('stress.c'),
      dependencies: [glib_static, sysprof],
      link_args: ['-static'],
    )

    custom_target(
      'initrd-stress.img',
      output: 'initrd-stress.img',
      input: stress,
      command: [find_program('initrd-stress.sh'), '@OUTPUT@', '@INPUT@'],
      build_by_default: true,
      depends: [stress],
    )
endif
