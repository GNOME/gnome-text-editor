icondir = join_paths(get_option('datadir'), 'icons/hicolor')

install_data(['org.gnome.TextEditor.svg',
              'org.gnome.TextEditor-symbolic.svg'],
  install_dir: join_paths(icondir, 'scalable/apps'))

if get_option('development')
  install_data(['org.gnome.TextEditor.Devel.svg'],
    install_dir: join_paths(icondir, 'scalable/apps'))
endif
