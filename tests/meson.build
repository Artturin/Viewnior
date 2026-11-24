test_helpers = static_library(
  'test-helpers',
  'test-helpers.c',
  dependencies: viewnior_deps
)

test_deps = [libviewnior_dep]

test_env = environment()
# GIO needs the MIME database to tell images from other files. It isn't
# on XDG_DATA_DIRS in the Nix build sandbox.
mime_prefix = dependency('shared-mime-info').get_pkgconfig_variable('prefix')
test_env.prepend('XDG_DATA_DIRS', join_paths(mime_prefix, 'share'))
test_env.set('GSETTINGS_BACKEND', 'memory')

foreach name : ['vnr-tools', 'vnr-file']
  exe = executable(
    'test-' + name,
    'test-' + name + '.c',
    dependencies: test_deps,
    link_with: test_helpers
  )
  test(name, exe, env: test_env)
endforeach
