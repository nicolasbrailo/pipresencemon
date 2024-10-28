// https://www.cs.uaf.edu/2016/fall/cs301/lecture/11_09_raspberry_pi.html

#include "gpio.h"

#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#define MOCK false
#define GPIO_PATH "/dev/gpiomem"
#define GPIO_MEM_SZ 4096
#define GPIO_INPUTS 13

struct GPIO {
  int fd;
  gpio_reg_t *mem;
};

struct GPIO *gpio_open() {
  const char *gpio_path = GPIO_PATH;
  struct GPIO *gpio = malloc(sizeof(struct GPIO));
  if (!gpio) {
    perror("GPIO bad alloc");
    return NULL;
  }

  if (MOCK) {
    return gpio;
  }

  gpio->fd = open(gpio_path, O_RDWR);
  if (gpio->fd < 0) {
    fprintf(stderr, "Error opening %s\n", gpio_path);
    perror("GPIO init fail");
    free(gpio);
    return NULL;
  }

  gpio->mem = mmap(NULL, GPIO_MEM_SZ, PROT_READ + PROT_WRITE, MAP_SHARED, gpio->fd, 0);
  if (!gpio->mem) {
    fprintf(stderr, "Can't mmap %s\n", gpio_path);
    perror("GPIO init fail");
    close(gpio->fd);
    free(gpio);
    return NULL;
  }

  (void)gpio->mem[GPIO_INPUTS];

  return gpio;
}

void gpio_close(struct GPIO *gpio) {
  if (MOCK) {
    free(gpio);
    return;
  }

  if (!gpio) {
    return;
  }

  if (munmap(gpio->mem, GPIO_MEM_SZ) != 0) {
    perror("GPIO close munmap fail");
  }

  if (close(gpio->fd) != 0) {
    perror("GPIO close fd fail");
  }

  free(gpio);
}

bool gpio_get_pin(struct GPIO *gpio, size_t pin) {
  if (MOCK) {
    FILE *file = fopen("gpio_mock", "r");
    if (file == NULL) {
      perror("ERROR: GPIO mocked, but file 'gpio_mock' can't be found. Do `echo 1 > gpio_mock` to "
             "mock.");
      return false;
    }
    char ch = fgetc(file);
    fclose(file);
    return (ch == '1');
  }

  return gpio->mem[GPIO_INPUTS] & (1 << pin);
}

gpio_reg_t gpio_get_inputs(struct GPIO *gpio) {
  if (MOCK) {
    return -1;
  }

  return gpio->mem[GPIO_INPUTS];
}

#define COL_NOO "\x1B[0m"
#define COL_RED "\x1B[31m"

gpio_reg_t gpio_get_and_print_delta(struct GPIO *gpio, gpio_reg_t prev_gpio_reg) {
  printf("%s ", COL_NOO);
  for (size_t i = 0; i < GPIO_PINS; ++i) {
    printf("P%02zu ", i);
  }
  printf("\n");

  gpio_reg_t gpio_ins = gpio_get_inputs(gpio);
  for (size_t i = 0; i < GPIO_PINS; ++i) {
    bool bit = (gpio_ins & (1 << i)) != 0;
    bool prev_bit = (prev_gpio_reg & (1 << i)) != 0;
    if (bit != prev_bit) {
      printf("%s  %u ", COL_RED, bit);
    } else {
      printf("%s  %u ", COL_NOO, bit);
    }
  }
  printf("\n");

  return gpio_ins;
}

#undef COL_NOO
#undef COL_RED
