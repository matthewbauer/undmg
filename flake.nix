{
  description = "Extract a DMG file";

  inputs.nixpkgs.url = "nixpkgs/nixos-21.05-small";

  outputs = { self, nixpkgs }: let
    systems = [ "x86_64-linux" "i686-linux" "x86_64-darwin" "aarch64-linux" "aarch64-darwin" ];
    forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f system);
  in {
    packages = forAllSystems (system: with (import nixpkgs { inherit system; }); {
      undmg = stdenv.mkDerivation {
        pname = "undmg";
        version = "1.1.0";

        nativeBuildInputs = [ pkgconfig ];
        buildInputs = [ zlib bzip2 lzfse lzma ];

        src = self;

        makeFlags = [ "PREFIX=$(out)" ];

        doCheck = true;
      };
    });

    defaultPackage = forAllSystems (system: self.packages.${system}.undmg);
  };
}
