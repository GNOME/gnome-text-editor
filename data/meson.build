desktop_file_in_config = configuration_data()
desktop_file_in_config.set('app_id', app_id)
desktop_file_in_config.set('app_name', 'org.gnome.TextEditor')
desktop_file_in_config.set('app_version', meson.project_version())
desktop_file_in = configure_file(
          input: 'org.gnome.TextEditor.desktop.in.in',
         output: '@0@.desktop.in'.format(app_id),
  configuration: desktop_file_in_config,
)


desktop_file = i18n.merge_file(
        input: desktop_file_in,
       output: '@0@.desktop'.format(app_id),
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

appconf = configuration_data()
appconf.set('app_id', app_id)
appstream_file_in = configure_file(
          input: 'org.gnome.TextEditor.appdata.xml.in.in',
         output: '@0@.appdata.xml.in'.format(app_id),
  configuration: appconf,
)
appstream_file = i18n.merge_file(
          input: appstream_file_in,
         output: '@0@.appdata.xml'.format(app_id),
         po_dir: '../po',
        install: true,
    install_dir: join_paths(get_option('datadir'), 'metainfo')
)

appstream_util = find_program('appstream-util', required: false)
if appstream_util.found()
  test('Validate appstream file', appstream_util,
    args: ['validate', '--nonet', appstream_file]
  )
endif

gnome.compile_schemas(build_by_default: true)
install_data('org.gnome.TextEditor.gschema.xml',
  install_dir: join_paths(get_option('datadir'), 'glib-2.0/schemas')
)

# D-Bus service file.
dbusconf = configuration_data()
dbusconf.set('bindir', join_paths(get_option('prefix'), get_option('bindir')))
dbusconf.set('app_id', app_id)
configure_file(
          input: 'org.gnome.TextEditor.service.in',
         output: '@0@.service'.format(app_id),
  configuration: dbusconf,
   install_dir: join_paths(get_option('datadir'), 'dbus-1', 'services'),
)

subdir('icons')
subdir('macos')
