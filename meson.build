project('ward', 'cpp',
  version : '0.1',
  default_options : ['warning_level=3', 'cpp_std=c++14'])

cxx = meson.get_compiler('cpp')
xinput_lib = cxx.find_library('Xi')
deps = [dependency('sdl2'), dependency('X11'), xinput_lib]
executable('ward',
           'main.cpp',
           dependencies: deps,
           install : true)
