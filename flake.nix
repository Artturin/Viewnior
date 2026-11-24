{
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    utils.url = "github:numtide/flake-utils";
  };

  outputs =
    inputs:
    inputs.utils.lib.eachDefaultSystem (
      system:
      let
        name = "viewnior";
        pkgs = import inputs.nixpkgs {
          localSystem = system;
          overlays = [
            (_: prev: {
              viewnior = (prev.viewnior.override { stdenv = prev.clangStdenv; }).overrideAttrs (old: {
                src = prev.nix-gitignore.gitignoreSource [ ] ./.;
                doCheck = true;
                # The window tests run on a virtual display
                nativeCheckInputs =
                  (old.nativeCheckInputs or [ ])
                  ++ prev.lib.optionals prev.stdenv.hostPlatform.isLinux [ prev.xvfb-run ];
              });
            })
          ];
        };
      in
      {
        packages = {
          default = inputs.self.packages."${system}".${name};
          "${name}" = pkgs.${name};
          viewnior-with-compile-commands = pkgs.viewnior-with-compile-commands;
        };

        devShells.default =
          with pkgs;
          mkShell.override { stdenv = pkgs.viewnior.stdenv; } {
            inputsFrom = [ inputs.self.packages."${system}".${name} ];
            nativeBuildInputs = [ clang-tools ] ++ lib.optionals stdenv.hostPlatform.isLinux [ xvfb-run ];
            env = {
              CLANGD_FLAGS = "--query-driver=${pkgs.lib.getExe pkgs.viewnior.stdenv.cc}";
            };
            # Work from the checkout's root, not $PWD: entering the shell from
            # a subdirectory such as builddir/ would otherwise set up a nested
            # build directory and point compile_commands.json at nothing.
            #
            # Meson saves PKG_CONFIG_PATH when it first sets up a directory
            # and ignores it afterwards, so once the shell's dependencies
            # change, builddir would keep building against the old store
            # paths. Record the path the directory was set up with and wipe
            # it when the shell's path differs.
            shellHook = ''
              if root=$(git rev-parse --show-toplevel 2>/dev/null) && [ -f "$root/meson.build" ]; then
                (
                  cd "$root" || exit
                  stamp=builddir/.devshell-pkg-config-path
                  if [ ! -f builddir/build.ninja ]; then
                    meson setup builddir >/dev/null
                  elif [ "$(cat "$stamp" 2>/dev/null)" != "$PKG_CONFIG_PATH" ]; then
                    echo "Dev shell dependencies changed, wiping builddir" >&2
                    meson setup --wipe builddir >/dev/null
                  fi && printf '%s' "$PKG_CONFIG_PATH" > "$stamp"
                  ln -fs "$root/builddir/compile_commands.json" "$root/compile_commands.json"
                )
              fi
            '';
          };

        formatter = pkgs.nixfmt-tree;
      }
    );
}
