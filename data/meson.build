if host_machine.system() not in ['windows', 'darwin']

desktop_file_in_config = configuration_data()
desktop_file_in_config.set('app_id', app_id)
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
          input: 'org.gnome.TextEditor.metainfo.xml.in.in',
         output: '@0@.metainfo.xml.in'.format(app_id),
  configuration: appconf,
)
appstream_file = i18n.merge_file(
          input: appstream_file_in,
         output: '@0@.metainfo.xml'.format(app_id),
         po_dir: '../po',
        install: true,
    install_dir: join_paths(get_option('datadir'), 'metainfo')
)

appstreamcli = find_program('appstreamcli', required: false)
if appstreamcli.found()
# url-redefined breaks from our .in.in files
#  test('Validate appstream file', appstreamcli,
#    args: ['validate', '--no-net', '--explain', appstream_file,
#            '--override', 'developer-name-tag-deprecated=info',
#            '--override', 'url-redefined=info'],
#    workdir: meson.current_build_dir()
#  )
endif

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

endif

gnome.compile_schemas(build_by_default: true)
install_data('org.gnome.TextEditor.gschema.xml',
  install_dir: join_paths(get_option('datadir'), 'glib-2.0/schemas')
)

subdir('icons')
subdir('macos')
