# wmvolt

An eye-candy dockapp to monitor Linux ACPI battery status.

![](README.screenshot1.png)

* Uses a "new" `/sys/class/power_supply/*` interface).
* Multiple batteries support.
* Custom backlight colors.
* An alert hook.
* FVWM3 support (via FvwmButtons or as a standalone app).

## Installation

You'll need pkg-config, libXpm-devel, libXext-devel & asciidoc.

~~~
$ make install
~~~

(The rpm spec is [here](https://github.com/gromnitsky/rpm).)

## News

* 0.0.2

    - Add -L option for a different backlight color when ac power is off.
    - Fix the ac power indicator.

## Credits

Big thanks to Thomas Nemeth
for [wmapmload](http://tnemeth.free.fr/projets/dockapps.html), from
which wmvolt borrows the GUI code.

## License

GPLv2+.
