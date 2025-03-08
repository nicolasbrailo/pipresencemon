.PHONY: run clean

all: pipresencemonsvc

run: pipresencemonsvc
	./pipresencemonsvc

clean:
	rm -rf build
	rm -f ./pipresencemonsvc
	rm -f ./example_svc

XCOMPILE=--sysroot /
XCOMPILE=\
  -target arm-linux-gnueabihf \
  -mcpu=arm1176jzf-s \
  --sysroot ~/src/xcomp-rpiz-env/mnt/ 

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

build/%.o: %.c %.h
	mkdir -p build
	@if [ ! -d ~/src/xcomp-rpiz-env/mnt/lib/raspberrypi-sys-mods ]; then \
		echo "xcompiler sysroot not detected, try `make xcompile-start`"; \
		@exit 1; \
	fi ;
	clang $(CFLAGS) $< -c -o $@

pipresencemonsvc: build/gpio.o \
			build/gpio_pin_active_monitor.o \
			build/cfg.o \
			build/occupancy_commands.o \
			pipresencemon.c
	clang $(CFLAGS) $^ -o $@

example_svc: example_svc.c
	$(CC) $(CFLAGS) $^ -o $@

.PHONY: format formatall xcompile-start xcompile-end deploytgt
format:
	git clang-format

formatall:
	ls *.c | xargs clang-format -i
	ls *.h | xargs clang-format -i

system-deps:
	sudo apt-get install clang-format

xcompile-start:
	./rpiz-xcompile/mount_rpy_root.sh ~/src/xcomp-rpiz-env

xcompile-end:
	./rpiz-xcompile/umount_rpy_root.sh ~/src/xcomp-rpiz-env

deploytgt: pipresencemonsvc pipresencemon.cfg
	scp ./pipresencemonsvc batman@10.0.0.146:/home/batman/pipresencemonsvc/
	scp ./pipresencemonsvc.cfg batman@10.0.0.146:/home/batman/pipresencemonsvc/

install_sysroot_deps:
	true

