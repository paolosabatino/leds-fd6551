# leds-fd6551
Kernel driver for fd6551 and compatible led driver devices

## Compilation

Just install the linux kernel headers from your distribution and then run:

```
make
```

## Installation

```
sudo make install
```

## Device tree entry

**First of all**: without a device tree entry, you won't go anywhere.

Chips can communicate with external device via an I²C bus; the usual
pattern is to connect them to an I²C master, thus on a real I²C bus,
or often just to two GPIO wires.

This example is the typical case where the chip is connected to a
board to GPIO wires. The GPIO wires can be turned into an I²C master
using the bit-banging i2c-gpio Linux driver.

```
/ {
	...

	i2c_fd6551: i2c-fd6651 {

		#address-cells = <1>;
		#size-cells = <0>;
		compatible = "i2c-gpio";

		sda-gpios = <&gpio2 21 (GPIO_ACTIVE_HIGH|GPIO_OPEN_DRAIN)>;
		scl-gpios = <&gpio2 22 (GPIO_ACTIVE_HIGH|GPIO_OPEN_DRAIN)>;

		i2c-gpio,sda-output-only;
		i2c-gpio,scl-output-only;
		i2c-gpio,delay-us = <1>;

		status="okay";

		fd6551@24 {

			compatible = "fdhisi,fd6551";
			reg = <0x24>, <0x33>, <0x34>, <0x35>, <0x36>;
			reg-names = "state", "icon-set", "dig1", "dig2", "dig3", "dig4";
			digits-reversed;

			clock {
				label = "fd6551::clock";
				bit = <0>;
			};

			usb {
				label = "fd6551::usb";
				bit = <1>;
			};

			pause {
				label = "fd6551::pause";
				bit = <2>;
			};

			play {
				label = "fd6551::play";
				bit = <3>;
			};

			colon {
				label = "fd6551::colon";
				bit = <4>;
			};

			network {
				label = "fd6551::net";
				bit = <5>;
			};

			wifi {
				label = "fd6551::wifi";
				bit = <6>;
			};

		};

	};

	...

}
```

Compatibles are `fdhisi,fd6551`, `fdhisi,fd650` and `titanmicro,tm1650`.

You need to adapt `sda-gpios` and `scl-gpios` for your particular case.
Also you may want to raise `i2c-gpio,delay-us` because this parameter
controls the delay between each bit. A value of `5` will drive the
bus at a safe 100KHz frequency.

`reg` entries (and similarly `reg-names`) can be omitted except for the first one.
In case they are omitted, the driver will use the default values. \
**Note**: `reg` are called `command` in the datasheets, and also in the
datasheets they are **multiplied by 2**. They are actually I²C addresses
because the chip will answer on different addresses on the bus.

`digits-reversed` is useful if the led panel has the digits in reversed
order.

Then come the icon leds, which follow the same specifications for regular
led nodes (eg: you can use `linux,default-trigger`, etc...).
You need although to specify the `bit` property which determines what
bit activates each icon within the icon set register of the chip.
You may also have different icons rather than those described in this
example. You'd probably want to do some experiments to find out which
is which.

## Usage

You can control the leds spawned by the driver the common way, so I won't describe
the details here, you can ask Google for that.
Accessing the directory `/sys/class/leds`, you will find the list of registered
leds:

```
paolo@rk3318-box:~$ ls /sys/class/leds
fd6551::clock  fd6551::colon  fd6551::net  fd6551::pause  fd6551::play  fd6551::usb  fd6551::wifi
```

The characters and brightness can instead be controlled accessing `/sys/bus/i2c/drivers/fd6551/*`:

```
root@rk3318-box:~# ls  /sys/bus/i2c/drivers/fd6551/4-0024/
brightness  chars  driver  leds  max_brightness  modalias  name  of_node  power  subsystem  uevent
```

**Note**: `4-0024` is a label assigned by the kernel depending upon the bus index assigned
to the I²C driver. In your case it may change and you would better add an `alias` in the
device tree to have a fixed bus number that does not change on every boot.


```
root@rk3318-box:~# echo 3 > /sys/bus/i2c/drivers/fd6551/4-0024/brightness
root@rk3318-box:~# echo OOPS > /sys/bus/i2c/drivers/fd6551/4-0024/chars
```


## Helpful references

* https://github.com/peter-kutak/FD650
* https://github.com/arthur-liberman/linux_openvfd
