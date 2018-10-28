shared_library('gstvalidatesniff',
               'gstvalidatesniff.c',
                include_directories : inc_dirs,
                c_args: ['-DHAVE_CONFIG_H'],
                install: true,
                install_dir: validate_plugins_install_dir,
                dependencies : [gst_dep, gst_pbutils_dep],
                link_with : [gstvalidate]
               )
