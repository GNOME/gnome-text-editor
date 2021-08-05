editor_sources = [
  'cjhtextregion.c',
  'editor-animation.c',
  'editor-application.c',
  'editor-application-actions.c',
  'editor-binding-group.c',
  'editor-buffer-monitor.c',
  'editor-buffer-scheduler.c',
  'editor-document.c',
  'editor-info-bar.c',
  'editor-frame-source.c',
  'editor-joined-menu.c',
  'editor-language-dialog.c',
  'editor-language-row.c',
  'editor-open-popover.c',
  'editor-page.c',
  'editor-page-actions.c',
  'editor-page-gsettings.c',
  'editor-page-settings.c',
  'editor-page-settings-provider.c',
  'editor-path.c',
  'editor-position-label.c',
  'editor-preferences-font.c',
  'editor-preferences-radio.c',
  'editor-preferences-spin.c',
  'editor-preferences-switch.c',
  'editor-print-operation.c',
  'editor-save-changes-dialog.c',
  'editor-search-bar.c',
  'editor-session.c',
  'editor-sidebar-item.c',
  'editor-sidebar-model.c',
  'editor-sidebar-row.c',
  'editor-signal-group.c',
  'editor-source-view.c',
  'editor-spell-checker.c',
  'editor-spell-cursor.c',
  'editor-spell-language.c',
  'editor-spell-language-info.c',
  'editor-spell-menu.c',
  'editor-spell-provider.c',
  'editor-text-buffer-spell-adapter.c',
  'editor-theme-selector.c',
  'editor-utils.c',
  'editor-window.c',
  'editor-window-actions.c',
  'editor-window-dnd.c',
  'main.c',
]

editor_deps = [
  cc.find_library('m', required: false),
  libglib_dep,
  libgtk_dep,
  libgtksourceview_dep,
  libadwaita_dep,
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

build_ident_h = vcs_tag(
     fallback: meson.project_version(),
        input: 'build-ident.h.in',
       output: 'build-ident.h',
)

subdir('enchant')
subdir('defaults')
subdir('modelines')
subdir('editorconfig')

editor = executable('gnome-text-editor', editor_sources + editor_enums + [build_ident_h],
  include_directories: [include_directories('..'),
                        include_directories('editorconfig'),
                        include_directories('editorconfig/libeditorconfig')],
               c_args: [ '-DHANDY_USE_UNSTABLE_API' ],
         dependencies: editor_deps,
              install: true,
)

test_spell_cursor = executable('test-spell-cursor', 'test-spell-cursor.c',
  dependencies: [libgtk_dep, libgtksourceview_dep],
  include_directories: [include_directories('..')],
)
test('test-spell-cursor', test_spell_cursor)
