KERNEL_DIR := /lib/modules/$(shell uname -r)/build

obj-m := gpio_timeline.o

all:
	make -C $(KERNEL_DIR) M=$(PWD) modules
clean:
	rm *.o *.ko
