#CFLAGS=-Wall -Werror -Wextra

.PHONY: run clean system-deps

outman_wproto.h: outman.xml
	wayland-scanner client-header < $< > $@
outman_wproto.c: outman.xml
	wayland-scanner private-code < $< > $@

run: test
	WAYLAND_DISPLAY="wayland-1" ./test
	#./test

soff:
	WAYLAND_DISPLAY="wayland-1" wlr-randr --output HDMI-A-1 --off
son:
	WAYLAND_DISPLAY="wayland-1" wlr-randr --output HDMI-A-1 --on

clean:
	rm -f *_wproto.* *.o test

test: gpio.o gpio_pin_active_monitor.o outman_wproto.o wl_display.o outman_wproto.h test.o
	$(CC) $(CFLAGS) $^ -o $@ -lwayland-client

system-deps:
	# wayland headers
	sudo apt-get install libwayland-dev
	# wlroots protocol files, needed for wlr-output-power-management-unstable-v1.xml
	sudo apt-get install libwlroots-dev
	# Install protocols.xml into /usr/share/wayland-protocols, not sure if needed
	sudo apt-get install wayland-protocols
