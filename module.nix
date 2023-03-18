{ config, pkgs, lib, ... }:

let
  cfg = config.nix.monitored;
in

{
  meta.maintainers = [ lib.maintainers.ners ];

  options.nix.monitored = with lib; {
    enable = mkEnableOption (mdDoc "nix-monitored, an improved output formatter for Nix");
    notify = mkEnableOption (mdDoc "notifications using libnotify") // {
      default = pkgs.stdenv.isLinux;
      defaultText = "pkgs.stdenv.isLinux";
    };
    package = mkPackageOption pkgs "nix-monitored" { };
  };

  config = lib.mkIf cfg.enable {
    nix.package = cfg.package.override { withNotify = cfg.notify; };
    nixpkgs.overlays = [
      (_: prev: {
        nixos-rebuild = prev.nixos-rebuild.override { nix = config.nix.package; };
        nix-direnv = prev.nix-direnv.override { nix = config.nix.package; };
      })
    ];
  };
}
