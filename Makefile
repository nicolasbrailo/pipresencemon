.PHONY: run clean

all: pipresencemonsvc

run: pipresencemonsvc
	./pipresencemonsvc

clean:
	rm -rf build
	rm -f ./pipresencemonsvc
	rm -f ./example_svc

XCOMPILE=\
  -target arm-linux-gnueabihf \
  -mcpu=arm1176jzf-s \
  --sysroot ~/src/xcomp-rpiz-env/mnt/ 

# Uncomment for local build
#XCOMPILE=

CFLAGS=\
	$(XCOMPILE)\
	-fdiagnostics-color=always \
	-ffunction-sections -fdata-sections \
	-ggdb -O3 \
	-std=gnu99 \
	-Wall -Werror -Wextra -Wpedantic \
	-Wendif-labels \
	-Wfloat-equal \
	-Wformat=2 \
	-Wimplicit-fallthrough \
	-Winit-self \
	-Winvalid-pch \
	-Wmissing-field-initializers \
	-Wmissing-include-dirs \
	-Wno-strict-prototypes \
	-Wno-unused-function \
	-Wno-unused-parameter \
	-Woverflow \
	-Wpointer-arith \
	-Wredundant-decls \
	-Wstrict-aliasing=2 \
	-Wundef \
	-Wuninitialized \

build/%.o: src/%.c
	mkdir -p build
	@if [ ! -d ~/src/xcomp-rpiz-env/mnt/lib/raspberrypi-sys-mods ]; then \
		echo "xcompiler sysroot not detected, try `make xcompile-start`"; \
		@exit 1; \
	fi ;
	clang $(CFLAGS) $< -c -o $@

pipresencemonsvc:\
	build/gpio.o \
	build/gpio_pin_active_monitor.o \
	build/json.o \
	build/cfg.o \
	build/occupancy_commands.o \
	build/pipresencemon.o
	clang $(CFLAGS) $^ -o $@ -ljson-c

example_svc: src/example_svc.c
	$(CC) $(CFLAGS) $^ -o $@

.PHONY: xcompile-start xcompile-end deploytgt

system-deps:
	sudo apt-get install clang-format

xcompile-start:
	./rpiz-xcompile/mount_rpy_root.sh ~/src/xcomp-rpiz-env

xcompile-end:
	./rpiz-xcompile/umount_rpy_root.sh ~/src/xcomp-rpiz-env

install_sysroot_deps:
	./rpiz-xcompile/add_sysroot_pkg.sh ~/src/xcomp-rpiz-env http://raspbian.raspberrypi.com/raspbian/pool/main/j/json-c/libjson-c-dev_0.16-2_armhf.deb

