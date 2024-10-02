obj-m += leds-fd6551.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

install:
	cp leds-fd6551.ko /lib/modules/$(shell uname -r)/kernel/drivers/leds
	depmod -a
