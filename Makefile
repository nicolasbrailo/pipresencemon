.PHONY: run clean

run: pipresencemon
	./pipresencemon

clean:
	rm -rf build
	rm -f ./pipresencemon

CFLAGS=-ggdb \
		   -Wall -Werror -Wextra \
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

build/%.o: %.c %.h
	mkdir -p build
	$(CC) $(CFLAGS) -isystem ./build $< -c -o $@

pipresencemon: build/gpio.o \
			build/gpio_pin_active_monitor.o \
			build/cfg.o \
			build/occupancy_commands.o \
			pipresencemon.c
	$(CC) $(CFLAGS) $^ -o $@

example_svc: example_svc.c
	$(CC) $(CFLAGS) $^ -o $@

.PHONY: format formatall
format:
	git clang-format

formatall:
	ls *.c | xargs clang-format -i
	ls *.h | xargs clang-format -i

system-deps:
	sudo apt-get install clang-format

