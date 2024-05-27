# PiPresenceMon

pipresencemon is a system utility for Rapsberry PIs that will monitor a GPIO sensor, to determine if a human being is likely nearby. If presence is detected, the screen will be switched on. If no presence is detected, the screen will turn off.

# Setup

This project requires a sensor; it is currently tested for a PIR sensor. PIR isn't ideal, because it relies on movement. If you stay fairly still, the screen will likely shut down.

The sensor should be connected to GPIO26 / pin 37. Today, the GPIO is hardcoded in pipresencemon.c. This utility is not quite ready to be used by normal human beings, yet. Only developers with a lot of resilience to survive my terrible code should look at this project for the time being.

# Build

* To get a build env ready, you can run `make system-deps` (the project assumes you already have a cross compiler or build essentials setup).
* To build, `make pipresencemon`
* To regen Wayland protocols, `rm wl_protos` and then `make clean pipresencemon`. It will flawlessly, at least 50% of the time.

# TODO
* Figure out why managing a single display in a multiple display setup breaks
* Capture user interaction for interactive mode
* Hook to dbus to capture wakelock and bcast ambient mode


