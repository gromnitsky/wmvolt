# wmvolt

An eye-candy dockapp to monitor Linux ACPI battery status.

![](http://ultraimg.com/images/2017/08/25/YIY6.png)

(The numbers above don't represent real battery values.)

* Uses a "new" `/sys/class/power_supply/*` interface).
* Multiple batteries support.
* User-selectable back-light color.
* Alert hook.
* FVWM support (via FvwmButtons or as a standalone app).

## Installation

You'll need libXpm-devel, libXext-devel & asciidoc.

~~~
$ make install
~~~

(The rpm spec is [here](https://github.com/gromnitsky/rpm).)

## Credits

Big thanks to Thomas Nemeth
for [wmapmload](http://tnemeth.free.fr/projets/dockapps.html), from
which wmvolt borrows the GUI code.

## License

GPLv2+.
