# undmg
Extract a DMG file.

## Usage
```sh
undmg ./Sublime\ Text\ Build\ 3103.dmg
```

## Building

This requires zlib, bzip2, lzfse, lzma, glibc, a linker, and a c compiler to build.

```sh
git clone https://github.com/matthewbauer/undmg.git
cd undmg
make
```

## Installing

```sh
make install
```

## DMG

Apple has never released official specs for the DMG file format. Most of it has been reverse engineered. The best source of documentation is at [newosxbook.com](http://newosxbook.com/DMG.html) and the [wikipedia page](https://en.wikipedia.org/wiki/Apple_Disk_Image). However, there are some DMG files that won't work correctly with this tool.

### What won't work

* Apple Data Compression (haven't found any using this)
* Signed DMG files (support is there from XPWN, I just haven't added a command line option)


## License
GPL3, most of this code is from the [xpwn](https://github.com/planetbeing/xpwn) project.
