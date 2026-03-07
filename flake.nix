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
              viewnior = (prev.viewnior.override { stdenv = prev.clangStdenv; }).overrideAttrs {
                src = prev.nix-gitignore.gitignoreSource [ ] ./.;
              };
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
            nativeBuildInputs = [ clang-tools ];
            env = {
              CLANGD_FLAGS = "--query-driver=${pkgs.lib.getExe pkgs.viewnior.stdenv.cc}";
            };
            shellHook = ''
              meson setup builddir >/dev/null
              ln -fs "$PWD/builddir/compile_commands.json" .
            '';
          };

        formatter = pkgs.nixfmt-tree;
      }
    );
}
