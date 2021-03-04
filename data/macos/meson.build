if host_machine.system() == 'darwin'

Info_plist = configure_file(
          input: 'Info.plist.in',
         output: 'Info.plist',
  configuration: desktop_file_in_config,
)

install_data('org.gnome.TextEditor.icns', install_dir : 'Contents/Resources')
install_data(Info_plist, install_dir : 'Contents')

endif
