icondir = join_paths(get_option('datadir'), 'icons/hicolor')

install_data(['org.gnome.TextEditor.svg'], install_dir: join_paths(icondir, 'scalable/apps'))
install_data(['org.gnome.TextEditor-symbolic.svg'], install_dir: join_paths(icondir, 'symbolic/apps'))

if get_option('development')
  install_data(['org.gnome.TextEditor.Devel.svg'], install_dir: join_paths(icondir, 'scalable/apps'))
endif
