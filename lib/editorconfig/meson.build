libeditorconfig_dep = dependency('editorconfig')

libeditor_deps += [libeditorconfig_dep]

libeditor_sources += files([
  'editorconfig-glib.c',
  'editor-editorconfig-addin.c',
])

libeditor_include_directories += [include_directories('.')]

config_h.set10('HAVE_EDITORCONFIG', true)
