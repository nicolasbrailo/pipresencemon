.PHONY: run clean

all: pipresencemon

run: pipresencemon
	./pipresencemon

clean:
	rm -rf build
	rm -f ./pipresencemon
	rm -f ./example_svc

XCOMPILE=\
  -target arm-linux-gnueabihf \
  -mcpu=arm1176jzf-s \
  --sysroot ~/src/xcomp-rpiz-env/mnt/ 
XCOMPILE=--sysroot /

CFLAGS=\
  $(XCOMPILE)\
  -ggdb \
  -Wall -Werror -Wextra \
  -Wundef \
  -Wmissing-include-dirs \
  -Wpointer-arith \
  -Winit-self \
  -Wfloat-equal \
  -Wredundant-decls \
  -Wimplicit-fallthrough \
  -Wendif-labels \
  -Wstrict-aliasing=2 \
  -Woverflow \
  -Wformat=2

build/%.o: %.c %.h
	mkdir -p build
	@if [ ! -d ~/src/xcomp-rpiz-env/mnt/lib/raspberrypi-sys-mods ]; then \
		echo "xcompiler sysroot not detected, try `make xcompile-start`"; \
		@exit 1; \
	fi ;
	clang $(CFLAGS) $< -c -o $@

pipresencemon: build/gpio.o \
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

deploytgt: pipresencemon pipresencemon.cfg
	scp ./pipresencemon batman@10.0.0.146:/home/batman/pipresencemon/
	scp ./pipresencemon.cfg batman@10.0.0.146:/home/batman/pipresencemon/

