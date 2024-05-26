.PHONY: run clean

run: pipresencemon
	./pipresencemon

clean:
	rm -rf build
	rm -f ./pipresencemon


CFLAGS=-Wall -Werror -Wextra \
       -Wundef \
       -Wlogical-op \
       -Wmissing-include-dirs \
       -Wpointer-arith \
       -Winit-self \
       -Wfloat-equal \
       -Wredundant-decls \
       -Wimplicit-fallthrough=2 \
       -Wendif-labels \
       -Wstrict-aliasing=2 \
       -Woverflow \
       -Wformat=2

# Download wl protocol definitions
wl_protos/wlr-output-management-unstable-v1.xml:
	mkdir -p $(shell dirname "$@")
	curl --output $@ \
		https://gitlab.freedesktop.org/wlroots/wlr-protocols/-/raw/2b8d43325b7012cc3f9b55c08d26e50e42beac7d/unstable/wlr-output-management-unstable-v1.xml?inline=false

# Build glue from wl protocol xml
build/wl_protos/%.c build/wl_protos/%.h: wl_protos/%.xml
	mkdir -p  build/wl_protos
	wayland-scanner private-code < $< > $@
	wayland-scanner client-header < $< > $(patsubst %.c,%.h,$@)

build build/wl_protos:
	mkdir -p build/wl_protos

build/%.o: %.c %.h
	mkdir -p build
	$(CC) $(CFLAGS) -isystem ./build $< -c -o $@

pipresencemon: build/wl_protos/wlr-output-management-unstable-v1.o \
			build/wl_ctrl.o \
			build/gpio.o \
			build/gpio_pin_active_monitor.o \
			pipresencemon.c
	$(CC) $(CFLAGS) $^ -o $@ -lwayland-client

.PHONY: format formatall
format:
	git clang-format

formatall:
	ls *.c | xargs clang-format -i
	ls *.h | xargs clang-format -i

# System tests
.PHONY: system-deps screenoff screenon
screenoff:
	WAYLAND_DISPLAY="wayland-1" wlr-randr --output HDMI-A-1 --off
screenon:
	WAYLAND_DISPLAY="wayland-1" wlr-randr --output HDMI-A-1 --on

system-deps:
	sudo apt-get install clang-format
	# wayland headers
	sudo apt-get install libwayland-dev
	# wlroots protocol files, needed for wlr-output-power-management-unstable-v1.xml
	#sudo apt-get install libwlroots-dev
	# Install protocols.xml into /usr/share/wayland-protocols, not sure if needed
	#sudo apt-get install wayland-protocols

