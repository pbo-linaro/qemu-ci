t = []
if get_option('plugins')
  foreach i : ['cache', 'drcov', 'execlog', 'hotblocks', 'hotpages', 'howvec',
               'hwprofile', 'ips', 'lockstep', 'stoptrigger']
    if host_os == 'windows'
      t += shared_module(i, files(i + '.c') + 'win32_linker.c',
                        include_directories: '../../include/qemu',
                        link_depends: [win32_qemu_plugin_api_lib],
                        link_args: ['-Lplugins', '-lqemu_plugin_api'],
                        dependencies: glib)

    else
      t += shared_module(i, files(i + '.c'),
                        include_directories: '../../include/qemu',
                        dependencies: glib)
    endif
  endforeach
endif
if t.length() > 0
  alias_target('contrib-plugins', t)
else
  run_target('contrib-plugins', command: find_program('true'))
endif
