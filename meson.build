project('TMaskCleanerMod', 'cpp',
    version : '1',
    default_options : ['buildtype=release', 'b_ndebug=if-release', 'cpp_std=c++17'],
)

# Sources
sources = [
    'TMaskCleanerMod/TMaskCleanerMod.cpp',
]

# Dependencies
vapoursynth_dep = dependency('vapoursynth').partial_dependency(compile_args : true, includes : true)

# Libs
shared_module('TMaskCleanerMod', sources,
    dependencies : [vapoursynth_dep],
    install : true,
    install_dir : join_paths(vapoursynth_dep.get_pkgconfig_variable('libdir'), 'vapoursynth'),
    gnu_symbol_visibility : 'hidden'
)
