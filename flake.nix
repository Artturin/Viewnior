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
            (final: prev: {
              viewnior = (prev.viewnior.override { stdenv = prev.clangStdenv; }).overrideAttrs {
                src = prev.nix-gitignore.gitignoreSource [ ] ./.;
              };
              viewnior-with-compile-commands = final.viewnior.overrideAttrs (_: {
                dontPatch = true;
                dontConfigure = true;
                dontBuild = true;
                dontFixup = true;
                installPhase = ''
                  meson setup build
                  mkdir -p $out/.build
                  # This will cause the output to depend on all the buildInputs
                  cp ./build/compile_commands.json $out/.build
                '';
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
            nativeBuildInputs = [ clang-tools ];
            env = {
              CLANGD_FLAGS = "--query-driver=${pkgs.lib.getExe pkgs.viewnior.stdenv.cc}";
            };
            shellHook = ''
              if [ ! -f compile_commands.json ]; then
                echo "Generating compile_commands.json..."
                nix build ".#viewnior-with-compile-commands" -o result-compile-commands
                # When directly linking without replacing directory `vnr-prefs.h` has `config.h file not found`
                #ln -sf "$(chase ./result-compile-commands)/.build/compile_commands.json" .
                cp ./result-compile-commands/.build/compile_commands.json .
                sed -i "/directory\":/c\"directory\": \"$PWD/builddir\"," compile_commands.json
                rm -f result-compile-commands
              fi
            '';
          };

        formatter = pkgs.nixfmt-tree;
      }
    );
}
