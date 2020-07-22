libeditorconfig_sources = [
  'ec_glob.c',
  'editorconfig.c',
  'editorconfig_handle.c',
  'ini.c',
  'misc.c',
]

libeditorconfig_deps = [
  dependency('libpcre')
]

# FIXME: Actually test these
libeditorconfig_args = [
  '-DHAVE_STRCASECMP',
  '-DHAVE_STRICMP',
  '-DHAVE_STRDUP',
  '-DHAVE_STRNDUP',
  '-Deditorconfig_VERSION_MAJOR=0',
  '-Deditorconfig_VERSION_MINOR=0',
  '-Deditorconfig_VERSION_PATCH=0',
  '-Deditorconfig_VERSION_SUFFIX=0',
]

if host_machine.system() == 'windows'
  libeditorconfig_args += ['-DWIN32']
else
  libeditorconfig_args += ['-DUNIX']
endif

libeditorconfig = static_library('editorconfig',
  libeditorconfig_sources,
  dependencies: libeditorconfig_deps,
        c_args: libeditorconfig_args,
           pic: true,
)

libeditorconfig_dep = declare_dependency(
            link_with: libeditorconfig,
  include_directories: include_directories('.'),
)
