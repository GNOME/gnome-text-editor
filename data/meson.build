desktop_file = i18n.merge_file(
  input: 'org.gnome.TextEditor.desktop.in',
  output: 'org.gnome.TextEditor.desktop',
  type: 'desktop',
  po_dir: '../po',
  install: true,
  install_dir: join_paths(get_option('datadir'), 'applications')
)

desktop_utils = find_program('desktop-file-validate', required: false)
if desktop_utils.found()
  test('Validate desktop file', desktop_utils,
    args: [desktop_file]
  )
endif

appstream_file = i18n.merge_file(
  input: 'org.gnome.TextEditor.appdata.xml.in',
  output: 'org.gnome.TextEditor.appdata.xml',
  po_dir: '../po',
  install: true,
  install_dir: join_paths(get_option('datadir'), 'appdata')
)

appstream_util = find_program('appstream-util', required: false)
if appstream_util.found()
  test('Validate appstream file', appstream_util,
    args: ['validate', appstream_file]
  )
endif

install_data('org.gnome.TextEditor.gschema.xml',
  install_dir: join_paths(get_option('datadir'), 'glib-2.0/schemas')
)

compile_schemas = find_program('glib-compile-schemas', required: false)
if compile_schemas.found()
  test('Validate schema file', compile_schemas,
    args: ['--strict', '--dry-run', meson.current_source_dir()]
  )
endif

# D-Bus service file.
dbusconf = configuration_data()
dbusconf.set('bindir', join_paths(get_option('prefix'), get_option('bindir')))
configure_file(
          input: 'org.gnome.TextEditor.service.in',
         output: 'org.gnome.TextEditor.service',
  configuration: dbusconf,
   install_dir: join_paths(get_option('datadir'), 'dbus-1', 'services'),
)

subdir('styles')
