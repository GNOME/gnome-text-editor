libeditor_deps = [
  cc.find_library('m', required: false),
  libglib_dep,
  libgtk_dep,
  libgtksourceview_dep,
  libadwaita_dep,
  libcairo_dep,
  libdex_dep,
  spelling_dep,
]

libeditor_sources = [
  'editor-document.c',
  'editor-document-operation.c',
  'editor-draft.c',
  'editor-progress.c',
  'editor-settings.c',
  'editor-settings-addin.c',
  'editor-source-buffer.c',
  'editor-source-location.c',
  'editor-source-view.c',
  'editor-zoom.c',
]

libeditor_include_directories = [
  include_directories('.'),
  include_directories('..'),
]

subdir('gsettings')
if get_option('editorconfig').enabled()
  subdir('editorconfig')
endif

libeditor_static = static_library('libeditor', libeditor_sources,
         dependencies: libeditor_deps,
  include_directories: libeditor_include_directories,
)
