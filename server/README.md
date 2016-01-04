# How to compile the server

## Install 3rd party tools

* Download and install [cc65][1] from github
* Download [Contiki OS](2) from github
* Download and install [c1541](3) (bundled with VICE)
* Setup the following environtment variables:
    * `CONTIKI`: Path to Contiki OS root
    * `CC65_HOME`: Path to cc65 root
    * `C1541`: Filepath (not just path) to the `c1541` utility (bundled with VICE)

Example:

```
export CONTIKI=~/src/c64/contiki
export CC65_HOME=~/src/cc65
export C1541=~/Applications/VICE/tools/c1541
```

### Mac only

* Install `make` from [brew](7), since `make` that comes with Xcode doesn't parse Contiki's makefiles correctly

```brew install make```

* Use `gmake` instead of `make`


## Compilation options

`make c64`: will generate a `.d64` file for the c64
`make c128`: will generate a `.d71` file for the c128


## Testing the server

### Real hardware requirements

* Install an [RR-NET](4) or [compatible](5) card in your c64 or c128, and load and run the server

### Emulator requirements

* Compile VICE with `--enable-ethernet` or download a [nightly build](6) which already has ethernet support
* Windows only: Install WinPCAP
* Mac & Linux only: Run `x64` / `x128` as root

eg:
```
$ sudo x64 vchar64d-c64.d64
```




[1]: https://github.com/cc65/cc65
[2]: https://github.com/contiki-os/contiki
[3]: http://vice-emu.sourceforge.net/
[4]: http://wiki.icomp.de/wiki/RR-Net
[5]: http://www.go4retro.com/products/64nic/
[6]: http://vice.pokefinder.org/
[7]: http://brew.sh/
