# wmvolt

An eye-candy dockapp to monitor Linux ACPI battery status.

![](http://ultraimg.com/images/2017/08/25/YIY6.png)

(The numbers above don't represent real battery values.)

* Linux only
* Uses a "new" `/sys/class/power_supply/*` interface)
* Multiple batteries support
* User-selectable back-light color
* FVWM support

## Installation

You'll need libXpm-devel, libXext-deve & asciidoc.

~~~
make install
~~~

## Credits

Big thanks to Thomas Nemeth
for [wmapmload](http://tnemeth.free.fr/projets/dockapps.html), from
which wmvolt borrows the GUI code.

## License

GPL2+.
