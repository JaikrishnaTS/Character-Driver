obj-m += char_driver.o

KERN_DIR = /lib/modules/$(shell uname -r)/build/

all:
	make -C $(KERN_DIR) M=$(PWD) modules
	gcc testmod.c -o testmod.out

clean:
	make -C $(KERN_DIR) M=$(PWD) clean
	rm -rf *.o testmod.out
