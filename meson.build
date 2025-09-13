project('gnome-text-editor', 'c',
          version: '49.1',
    meson_version: '>= 0.60.0',
  default_options: [ 'warning_level=2', 'c_std=gnu17' ],
)

cc = meson.get_compiler('c')

gnome = import('gnome')
i18n = import('i18n')

if get_option('development')
  app_id = 'org.gnome.TextEditor.Devel'
else
  app_id = 'org.gnome.TextEditor'
endif
# Various optimizations for non-debug builds including disabling of
# cast checks, asserts, and additional link options.
release_link_args = []
test_link_args = [
  '-Wl,-z,defs',   # Detect and reject underlinking
  '-Wl,-z,now',    # Disable lazy binding
  '-Wl,-z,relro',  # Read-only segments after relocation
]
if not get_option('buildtype').startswith('debug')
  add_global_arguments([
    '-DG_DISABLE_ASSERT',
    '-DG_DISABLE_CAST_CHECKS',
  ], language: 'c')
  test_link_args += ['-Wl,-Bsymbolic', '-fno-plt']
endif
# Check dependencies
glib_req_version = '2.80'
gtk_req_version = '4.19.4'
adwaita_req_version = '1.8.alpha'
gtksourceview_req_version = '5.15.0'
spelling_req_version = '0.4.0'

glib_req = '>= @0@'.format(glib_req_version)
gtk_req = '>= @0@'.format(gtk_req_version)
gtksourceview_req = '>= @0@'.format(gtksourceview_req_version)
adwaita_req = '>= @0@'.format(adwaita_req_version)
spelling_req = '>= @0@'.format(spelling_req_version)

libglib_dep = dependency('gio-2.0', version: glib_req)
libgtk_dep = dependency('gtk4', version: gtk_req)
libgtksourceview_dep = dependency('gtksourceview-5', version: gtksourceview_req)
libadwaita_dep = dependency('libadwaita-1', version: adwaita_req)
spelling_dep = dependency('libspelling-1', version: spelling_req)

# Specify minimum library versions
glib_major = glib_req_version.split('.')[0].to_int()
glib_minor = glib_req_version.split('.')[1].to_int()
gtk_major = gtk_req_version.split('.')[0].to_int()
gtk_minor = gtk_req_version.split('.')[1].to_int()
adw_major = adwaita_req_version.split('.')[0].to_int()
adw_minor = adwaita_req_version.split('.')[1].to_int()
gtksourceview_major = gtksourceview_req_version.split('.')[0].to_int()
gtksourceview_minor = gtksourceview_req_version.split('.')[1].to_int()

if glib_minor % 2 == 1
  glib_minor = glib_minor + 1
endif
if gtk_minor % 2 == 1
  gtk_minor = gtk_minor + 1
endif
if adw_minor % 2 == 1
  adw_minor = adw_minor + 1
endif
if gtksourceview_minor % 2 == 1
  gtksourceview_minor = gtksourceview_minor + 1
endif

config_h = configuration_data()
config_h.set_quoted('APP_ID', app_id)
config_h.set_quoted('PACKAGE_VERSION', meson.project_version())
config_h.set_quoted('GETTEXT_PACKAGE', 'gnome-text-editor')
config_h.set_quoted('LOCALEDIR', join_paths(get_option('prefix'), get_option('localedir')))
config_h.set_quoted('PACKAGE_WEBSITE', 'https://gitlab.gnome.org/GNOME/gnome-text-editor')
config_h.set_quoted('PACKAGE_BUGREPORTS', get_option('bugreport_url'))
config_h.set_quoted('PACKAGE_ICON_NAME', app_id)
config_h.set_quoted('PACKAGE_NAME', 'Text Editor')
config_h.set_quoted('PACKAGE_DATADIR', join_paths(get_option('prefix'), get_option('datadir'), 'gnome-text-editor'))
config_h.set10('DEVELOPMENT_BUILD', get_option('development'))

global_c_args = [
  '-DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_@0@_@1@'.format(glib_major, glib_minor),
  '-DGLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_@0@_@1@'.format(glib_major, glib_minor),
  '-DGDK_VERSION_MIN_REQUIRED=GDK_VERSION_@0@_@1@'.format(gtk_major, gtk_minor),
  '-DGDK_VERSION_MAX_ALLOWED=GDK_VERSION_@0@_@1@'.format(gtk_major, gtk_minor),
  '-DADW_VERSION_MIN_REQUIRED=ADW_VERSION_@0@_@1@'.format(adw_major, adw_minor),
  '-DADW_VERSION_MAX_ALLOWED=ADW_VERSION_@0@_@1@'.format(adw_major, adw_minor),
  '-DGTK_SOURCE_VERSION_MIN_REQUIRED=GTK_SOURCE_VERSION_@0@_@1@'.format(gtksourceview_major, gtksourceview_minor),
  '-DGTK_SOURCE_VERSION_MAX_ALLOWED=GTK_SOURCE_VERSION_@0@_@1@'.format(gtksourceview_major, gtksourceview_minor),
]
test_c_args = [
  '-Wcast-align',
  '-Wdeclaration-after-statement',
  '-Werror=address',
  '-Werror=array-bounds',
  '-Werror=empty-body',
  '-Werror=implicit',
  '-Werror=implicit-function-declaration',
  # '-Werror=incompatible-pointer-types',
  '-Werror=init-self',
  '-Werror=int-conversion',
  '-Werror=int-to-pointer-cast',
  '-Werror=main',
  '-Werror=misleading-indentation',
  '-Werror=missing-braces',
  '-Werror=nonnull',
  '-Werror=overflow',
  '-Werror=parenthesis',
  '-Werror=pointer-arith',
  '-Werror=pointer-to-int-cast',
  '-Werror=return-type',
  '-Werror=sequence-point',
  '-Werror=shadow',
  '-Werror=strict-prototypes',
  '-Werror=trigraphs',
  '-Werror=undef',
  '-Werror=write-strings',
  '-Wformat-nonliteral',
  ['-Werror=format-security', '-Werror=format=2' ],
  '-Wignored-qualifiers',
  '-Wimplicit-function-declaration',
  '-Wlogical-op',
  '-Wmissing-format-attribute',
  '-Wmissing-noreturn',
  '-Wnested-externs',
  '-Wno-cast-function-type',
  '-Wno-missing-field-initializers',
  '-Wno-sign-compare',
  '-Wno-unused-parameter',
  '-Wold-style-definition',
  '-Wpointer-arith',
  '-Wstrict-prototypes',
  '-Wswitch-default',
  '-Wswitch-enum',
  '-Wundef',
  '-Wuninitialized',
  '-Wunused',
  '-Wno-deprecated-declarations',
  '-fno-strict-aliasing',
]
if get_option('buildtype') != 'plain'
  test_c_args += '-fstack-protector-strong'
endif

foreach arg: test_c_args
  if cc.has_multi_arguments(arg)
    global_c_args += arg
  endif
endforeach

if cc.has_multi_arguments('-Wmissing-declarations')
  global_c_args += '-Wmissing-declarations'
endif

add_project_arguments(global_c_args, language: 'c')

foreach link_arg: test_link_args
  if cc.has_link_argument(link_arg)
    release_link_args += link_arg
  endif
endforeach

subdir('data')
subdir('src')
subdir('help')
subdir('po')

configure_file(
         output: 'config.h',
  configuration: config_h,
)

gnome.post_install(
  gtk_update_icon_cache: true,
  glib_compile_schemas: true,
  update_desktop_database: host_machine.system() not in ['darwin', 'windows'],
)
