{ nixosTest
, expect
, nix-monitored
, nix
, nix-output-monitor
, ...
}:

nixosTest {
  name = "nix-monitored";

  nodes =
    let
      mkNode = notify: {
        imports = [ ./module.nix ];
        nix.monitored = {
          enable = true;
          inherit notify;
          package = nix-monitored;
        };
        environment.systemPackages = [ expect ];
      };
    in
    {
      withNotify = mkNode true;
      withoutNotify = mkNode false;
    };

  testScript =
    let
      mkPkg = notify: nix-monitored.override { withNotify = notify; };
    in
    ''
      start_all()

      machines = [withNotify, withoutNotify]
      packages = ["${mkPkg true}", "${mkPkg false}"]

      for (machine, package) in zip(machines, packages):
        for binary in ["nix", "nix-build", "nix-shell"]:
          actual = machine.succeed(f"readlink $(which {binary})").strip()
          expected = f"{package}/bin/{binary}"
          assert expected == actual, f"{binary} binary is {actual}, expected {expected}"

        actual = machine.succeed("unbuffer nix --version").strip()
        expected = "nix-output-monitor ${nix-output-monitor.version}\nnix (Nix) ${nix.version}"
        assert expected == actual, f"version string is {actual}, expected {expected}"
    '';
}
