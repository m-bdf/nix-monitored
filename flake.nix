{
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
    nix-filter.url = "github:numtide/nix-filter";
    pre-commit-hooks = {
      url = "github:cachix/pre-commit-hooks.nix";
      inputs.nixpkgs.follows = "nixpkgs";
      inputs.flake-utils.follows = "flake-utils";
    };
  };

  outputs = inputs:
    inputs.flake-utils.lib.eachSystem
      (builtins.attrNames inputs.nixpkgs.legacyPackages)
      (system:
        let
          pkgs = inputs.nixpkgs.legacyPackages.${system}.extend
            inputs.self.overlays.default;
        in
        rec {
          packages = {
            inherit (pkgs) nix-monitored;
            default = packages.nix-monitored;
          };

          checks = {
            nixosTest = pkgs.callPackage ./test.nix { };

            pre-commit = inputs.pre-commit-hooks.lib.${system}.run {
              src = ./.;
              hooks = {
                nixpkgs-fmt.enable = true;
                clang-format.enable = true;
              };
            };
          };

          devShells = {
            nix-monitored = pkgs.mkShell.override { stdenv = pkgs.gccStdenv; } {
              name = "nix-monitored";
              inputsFrom = [ packages.default ];
              nativeBuildInputs = with pkgs; [ clang-tools nixpkgs-fmt ];
              inherit (checks.pre-commit) shellHook;
            };
            default = devShells.nix-monitored;
          };

          formatter = pkgs.nixpkgs-fmt;
        }
      )
    //
    rec {
      overlays = {
        nix-monitored = _: prev:
          let pkgs = prev.extend inputs.nix-filter.overlays.default;
          in { nix-monitored = pkgs.callPackage ./default.nix { }; };
        default = overlays.nix-monitored;
      };

      nixosModules = {
        nix-monitored = {
          nixpkgs.overlays = [ overlays.default ];
          imports = [ ./module.nix ];
        };
        default = nixosModules.nix-monitored;
      };
      darwinModules = nixosModules;
    };
}
