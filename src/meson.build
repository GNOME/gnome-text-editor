editor_sources = [
  'editor-animation.c',
  'editor-application.c',
  'editor-application-actions.c',
  'editor-bin.c',
  'editor-binding-group.c',
  'editor-document.c',
  'editor-filter-model.c',
  'editor-frame-source.c',
  'editor-language-dialog.c',
  'editor-language-row.c',
  'editor-page.c',
  'editor-page-actions.c',
  'editor-page-gsettings.c',
  'editor-page-settings.c',
  'editor-page-settings-provider.c',
  'editor-path.c',
  'editor-preferences-font.c',
  'editor-preferences-radio.c',
  'editor-preferences-row.c',
  'editor-preferences-spin.c',
  'editor-preferences-switch.c',
  'editor-preferences-window.c',
  'editor-print-operation.c',
  'editor-search-bar.c',
  'editor-session.c',
  'editor-sidebar.c',
  'editor-sidebar-item.c',
  'editor-sidebar-model.c',
  'editor-sidebar-row.c',
  'editor-signal-group.c',
  'editor-tab.c',
  'editor-tab-actions.c',
  'editor-utils.c',
  'editor-window.c',
  'editor-window-actions.c',
  'main.c',
]

editor_deps = [
  cc.find_library('m', required: false),
  dependency('gio-2.0', version: '>= 2.50'),
  dependency('gtk+-3.0', version: '>= 3.24'),
  dependency('gtksourceview-4', version: '>= 4.6'),
]

editor_sources += gnome.compile_resources('editor-resources',
  'org.gnome.TextEditor.gresource.xml',
  c_name: 'editor'
)

editor_enums_headers = [
  'editor-search-bar-private.h',
]

editor_enums = gnome.mkenums_simple('editor-enums',
     body_prefix: '#include "config.h"',
   header_prefix: '#include "editor-types.h"',
       decorator: 'extern',
         sources: editor_enums_headers,
)

editor = executable('gnome-text-editor', editor_sources + editor_enums,
  include_directories: include_directories('..'),
         dependencies: editor_deps,
              install: true,
)
