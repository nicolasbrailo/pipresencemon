CFLAGS=-Wall -Werror -Wextra

.PHONY: run clean system-deps

run: test
	./test

clean:
	rm -f *.o test

test: test.o gpio.o gpio_pin_active_monitor.o
	$(CC) $(CFLAGS) $? -o $@

system-deps:
	# wayland headers
	sudo apt-get install libwayland-dev

