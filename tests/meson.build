test_helpers = static_library(
  'test-helpers',
  'test-helpers.c',
  'test-exif-helpers.cpp',
  dependencies: viewnior_deps
)

test_deps = [libviewnior_dep]

# GIO needs the MIME database to tell images from other files. It isn't
# on XDG_DATA_DIRS in the Nix build sandbox.
mime_prefix = dependency('shared-mime-info').get_pkgconfig_variable('prefix')
mime_data_dir = join_paths(mime_prefix, 'share')

test_env = environment()
test_env.prepend('XDG_DATA_DIRS', mime_data_dir)
test_env.set('GSETTINGS_BACKEND', 'memory')

foreach source : ['vnr-tools.c', 'vnr-file.c', 'uni-exiv2.cpp']
  name = source.split('.')[0]
  exe = executable(
    'test-' + name,
    'test-' + source,
    dependencies: test_deps,
    link_with: test_helpers
  )
  test(name, exe, env: test_env)
endforeach

# The window tests need a display. They run on a virtual one from
# xvfb-run, and are skipped without it rather than opening windows on
# the user's display.
window_env = environment()
window_env.prepend('XDG_DATA_DIRS', mime_data_dir)
window_env.set('GSETTINGS_BACKEND', 'memory')
window_env.set('GDK_BACKEND', 'x11')
window_env.set('NO_AT_BRIDGE', '1')

test_window = executable(
  'test-vnr-window',
  'test-vnr-window.c',
  dependencies: test_deps,
  link_with: test_helpers
)

xvfb_run = find_program('xvfb-run', required: false)
if xvfb_run.found()
  test('vnr-window', xvfb_run,
    args: ['-a', '-s', '-screen 0 1280x1024x24', test_window],
    env: window_env,
    timeout: 120
  )
else
  message('xvfb-run not found, the window tests will be skipped')
  window_env.set('VIEWNIOR_TEST_SKIP', 'xvfb-run not found, skipping the window tests')
  test('vnr-window', test_window, env: window_env)
endif
