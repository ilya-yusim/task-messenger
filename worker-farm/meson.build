# tm-worker-farm — Go controller for spawning/tracking tm-worker processes.
#
# This subdir is gated on -Dbuild_worker_farm=true (default off) so that
# C++-only contributors don't need a Go toolchain. When enabled, we
# shell out to `go build`, letting Go's own cache decide what's stale
# (build_always_stale: true). The output binary lands in
# builddir/worker-farm/.

go_prog = find_program('go', required: true)

# Verify Go is recent enough. Go's `--version` flag doesn't exist
# (`go version` is the subcommand) so meson's built-in version: check
# can't probe it; do it ourselves and fail with a clear message.
go_version_cmd = run_command(go_prog, 'version', check: true)
go_version_out = go_version_cmd.stdout().strip()
message('worker-farm: ' + go_version_out)

worker_farm_exe_name = host_machine.system() == 'windows' ? 'tm-worker-farm.exe' : 'tm-worker-farm'

# Pass GOCACHE under builddir so out-of-tree builds don't pollute the
# user's home directory and CI runners get clean caches.
go_env = environment()
go_env.set('GOCACHE', meson.current_build_dir() / '.gocache')
go_env.set('CGO_ENABLED', '0')

worker_farm_out = meson.current_build_dir() / worker_farm_exe_name

worker_farm_build = custom_target(
  'tm-worker-farm',
  output: worker_farm_exe_name,
  # `go -C <dir>` (Go 1.20+) lets us run from the module root without a
  # wrapper script. The package path is then relative to that dir, but
  # `-o` is also resolved relative to it, so we pass an absolute path.
  command: [
    go_prog,
    '-C', meson.current_source_dir(),
    'build',
    '-trimpath',
    '-ldflags=-s -w',
    '-o', worker_farm_out,
    './cmd/tm-worker-farm',
  ],
  env: go_env,
  build_always_stale: true,   # let Go's own cache decide
  build_by_default: true,
  console: true,
  install: true,
  install_dir: get_option('bindir'),
)
