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

# The window tests need a display. They run on a virtual one when
# xvfb-run is installed, and skip themselves when there is no display.
window_env = test_env
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
  test('vnr-window', test_window, env: window_env, timeout: 120)
endif
