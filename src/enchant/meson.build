if get_option('enchant').enabled()

libenchant_dep = dependency('enchant-2', version: enchant_req)
libicu_dep = dependency('icu-uc')

editor_deps += [libenchant_dep, libicu_dep]

editor_sources += files([
  'editor-enchant-spell-language.c',
  'editor-enchant-spell-provider.c',
])

editor_c_args += ['-DHAVE_ENCHANT']

endif
