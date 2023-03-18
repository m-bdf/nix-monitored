{ gccStdenv
, lib
, nix-filter
, nix
, nix-output-monitor
, withNotify ? gccStdenv.isLinux
, libnotify
, nixos-icons
, ...
}:

gccStdenv.mkDerivation {
  pname = "nix-monitored";

  src = nix-filter {
    root = ./.;
    include = [ "monitored.cc" "Makefile" ];
  };

  inherit (nix) version outputs;

  CXXFLAGS = [ "-O2" ] ++ lib.optionals withNotify [ "-DNOTIFY" ];

  makeFlags = [
    "BIN=nix"
    "BINDIR=$(out)/bin"
    "NIXPATH=${lib.makeBinPath [ nix nix-output-monitor ]}"
  ] ++ lib.optionals withNotify [
    "NOTIFY_ICON=${nixos-icons}/share/icons/hicolor/32x32/apps/nix-snowflake.png"
  ];

  postInstall = ''
    ln -s $out/bin/nix $out/bin/nix-build
    ln -s $out/bin/nix $out/bin/nix-shell
    ls ${nix} | while read d; do
      [ -e "$out/$d" ] || ln -s ${nix}/$d $out/$d
    done
    ls ${nix}/bin | while read b; do
      [ -e $out/bin/$b ] || ln -s ${nix}/bin/$b $out/bin/$b
    done
  '' + lib.pipe nix.outputs [
    (builtins.map (o: ''
      [ -e "''$${o}" ] || ln -s ${nix.${o}} ''$${o}
    ''))
    (builtins.concatStringsSep "\n")
  ];

  # Nix will try to fixup the propagated outputs (e.g. nix-dev), to which it has
  # no write permission when building this derivation.
  # We don't actually need any fixup, as the derivation we are building is a native Nix build,
  # and all the propagated outputs have already been fixed up for the Nix derivation.
  dontFixup = true;

  meta.mainProgram = "nix";
}
