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
              viewnior = prev.viewnior.overrideAttrs {
                src = prev.lib.cleanSource ./.;
              };
            })
          ];
        };
      in
      {
        packages = {
          default = inputs.self.packages."${system}".${name};
          "${name}" = pkgs.${name};
        };

        devShells.default =
          with pkgs;
          mkShell {
            inputsFrom = [ self.packages."${system}".${name} ];
          };

        formatter = pkgs.nixfmt-tree;
      }
    );
}
