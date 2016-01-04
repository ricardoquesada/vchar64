# How to compile the server

## Install 3rd party tools

* Download and install [cc65][1] from github
* Download [Contiki OS][2] from github
* Download and install [c1541][3] (bundled with VICE)
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

* Install `make` from [brew][7], since `make` that comes with Xcode doesn't parse Contiki's makefiles correctly

```brew install make```

* Use `gmake` instead of `make`


## Compilation options

`make c64`: will generate a `.d64` file for the c64
`make c128`: will generate a `.d71` file for the c128


## Testing the server

### Hardware requirements

* Install an [RR-Net][4] (or [compatible][5]) or an TFE or an ETH64 card in your c64 or c128, and load and run the server

### Emulator requirements

* Compile VICE with `--enable-ethernet` or download a [nightly build][6] which already has ethernet support
* Windows only: Install WinPCAP
* Mac & Linux only: Run `x64` / `x128` as root

eg:
```
$ sudo x64 vchar64d-c64.d64
```

### What's inside the .d64 / .d71 images

* `vchar64-server`: the server
* `ethconfig`: to setupt the ethernet driver. Not needed if you are already have an RR-Net card
* `ipconfig`: the DHCP client, needed in order to setup the IP address. Only run it once.
* `contiki.cfg`: Configuration file that stores the IP address.
* `cs8900a.eth`: TFE / RR-Net driver (?)
* `lan91c96.eth: ETH64 driver (?)

### Running the server

1. Setup the Ethernet card with `ethconfig` (not needed if your card is RR-Net):

```
LOAD"ETHCONFIG",8,1
RUN
```

2. Setup the IP address with `ipconfig`:

```
LOAD"IPCONFIG",8,1
RUN
```

3. Run the server

```
LOAD"VCHAR64-SERVER",8,1
RUN
```
There is no need to run steps 1) and 2) again once you have setup the ethernet card and IP address.


[1]: https://github.com/cc65/cc65
[2]: https://github.com/contiki-os/contiki
[3]: http://vice-emu.sourceforge.net/
[4]: http://wiki.icomp.de/wiki/RR-Net
[5]: http://www.go4retro.com/products/64nic/
[6]: http://vice.pokefinder.org/
[7]: http://brew.sh/
