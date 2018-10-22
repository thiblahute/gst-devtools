html_theme = 'https://github.com/hotdoc/hotdoc_lumen_theme/releases/download/0.6/hotdoc_lumen_theme-0.6.tar.xz?sha256=0e2f175f4cf8c00ed7ac5014e30c806a294b0d3818565eb74e1424a948e8a452'
hotdoc_p = find_program('hotdoc', required: get_option('doc'))
build_hotdoc = false

if not hotdoc_p.found()
    message('Hotdoc not found, not building the documentation')
    subdir_done()
endif

hotdoc_extension_dir = []
required_hotdoc_extensions = ['gi-extension']

hotdoc = import('hotdoc')
if not hotdoc.has_extensions(required_hotdoc_extensions)
    if get_option('doc').enabled()
        error('Documentation enabled but gi-extension missing')
    endif

    message('@0@ extensions not found, not building documentation'.format(required_hotdoc_extensions))
    subdir_done()
endif

if not build_gir
    if get_option('doc').enabled()
        error('Documentation enabled but introspection not built.')
    endif

    message('Introspection not built, can\'t build the documentation')
    subdir_done()
endif

excludes = ['gettext.h',
    'gst-validate-internal.h',
    'gst-validate-override-registry.h',
    'gst-validate-utils.h',
    'gst-validate-media-info.h',
    'media-descriptor.h',
    'media-descriptor-parser.h',
    'media-descriptor-writer.h',
    'gst-validate-i18n-lib.h',
    'gst-validate-override-registry.c',
    'gst-validate-utils.c',
    'gst-validate-report.c',
    'gst-validate-media-info.c',
    'media-descriptor.c',
    'media-descriptor-parser.c',
    'media-descriptor-writer.c',
    'gst-validate-i18n-lib.c'
]

build_hotdoc = true
validate_excludes = []
foreach f: excludes
    validate_excludes += [join_paths(meson.current_source_dir(), '..',
        'validate', 'gst', 'validate', f)]
endforeach

validate_sources = []
foreach f: gstvalidate_headers + gstvalidate_sources
    validate_sources += [join_paths(meson.current_source_dir(), '..',
        'validate', 'gst', 'validate', f)]
endforeach

hotdoc = import('hotdoc')
plugins_doc = []
all_plugins_paths = []
libs_doc = [hotdoc.generate_doc('gst-devtools',
    project_version: apiversion,
    sitemap: 'sitemap.txt',
    index: 'index.md',
    gi_c_sources: validate_sources,
    gi_c_source_filters: validate_excludes,
    gi_index: 'gi-index.md',
    gi_smart_index: true,
    gi_sources: [validate_gir[0].full_path()],
    disable_incremental_build: true,
    dependencies : [validate_dep],
    html_theme: html_theme,
)]
