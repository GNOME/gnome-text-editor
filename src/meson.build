editor_sources = [
  'editor-animation.c',
  'editor-application.c',
  'editor-application-actions.c',
  'editor-buffer-monitor.c',
  'editor-document.c',
  'editor-document-statistics.c',
  'editor-encoding-dialog.c',
  'editor-encoding-model.c',
  'editor-encoding-row.c',
  'editor-indent-model.c',
  'editor-info-bar.c',
  'editor-file-manager.c',
  'editor-focus-chain.c',
  'editor-frame-source.c',
  'editor-fullscreen-box.c',
  'editor-joined-menu.c',
  'editor-language-dialog.c',
  'editor-language-row.c',
  'editor-newline-model.c',
  'editor-open-popover.c',
  'editor-page.c',
  'editor-page-actions.c',
  'editor-page-gsettings.c',
  'editor-page-settings.c',
  'editor-page-settings-provider.c',
  'editor-page-vim.c',
  'editor-path.c',
  'editor-position-label.c',
  'editor-preferences-dialog.c',
  'editor-preferences-font.c',
  'editor-preferences-radio.c',
  'editor-preferences-spin.c',
  'editor-preferences-switch.c',
  'editor-properties.c',
  'editor-properties-panel.c',
  'editor-print-operation.c',
  'editor-recoloring.c',
  'editor-save-changes-dialog.c',
  'editor-search-bar.c',
  'editor-search-entry.c',
  'editor-search-model.c',
  'editor-session.c',
  'editor-sidebar-item.c',
  'editor-sidebar-model.c',
  'editor-sidebar-row.c',
  'editor-source-iter.c',
  'editor-source-map.c',
  'editor-source-view.c',
  'editor-statusbar.c',
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
  spelling_dep,
]

editor_sources += gnome.compile_resources('editor-resources',
  'org.gnome.TextEditor.gresource.xml',
  c_name: 'editor'
)

editor_enums_headers = [
  'editor-search-bar-private.h',
  'editor-types.h',
]

editor_enums = gnome.mkenums_simple('editor-enums',
     body_prefix: '#include "config.h"',
   header_prefix: '#include "editor-types.h"',
       decorator: 'extern',
         sources: editor_enums_headers,
)

build_ident_h = vcs_tag(
      command: ['git', 'describe'],
     fallback: meson.project_version(),
        input: 'build-ident.h.in',
       output: 'build-ident.h',
)

if host_machine.system() == 'windows'
  rc_data = configuration_data()
  rc_data.set('GTE_MAJOR_VERSION', meson.project_version().split('.')[0].to_int())
  rc_data.set('GTE_VERSION', meson.project_version())
  rc_data.set('GTE_ICON', meson.project_source_root() / 'build-aux/win32/org.gnome.TextEditor.ico')

  win32rc = configure_file(input: 'win32.rc.in',
    output: 'win32.rc',
    configuration: rc_data,
  )

  win32res = import('windows').compile_resources(win32rc)

  editor_sources += win32res
endif

editor_c_args = []

subdir('defaults')
subdir('modelines')

if get_option('editorconfig').enabled()
  subdir('editorconfig')
endif

editor = executable('gnome-text-editor', editor_sources + editor_enums + [build_ident_h],
  include_directories: [include_directories('..'),
                        include_directories('editorconfig')],
               c_args: editor_c_args,
         dependencies: editor_deps,
        win_subsystem: 'windows',
              install: true,
)
