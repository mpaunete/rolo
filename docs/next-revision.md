# Next revision

Planned changes for the next version of the hardware.

## Electronics (controller PCB)

- [ ] Swap the PDN and UART connections on the TMC2209 drivers
      (and check whether UART should be disconnected)
- [ ] Battery voltage divider on the board, to GPIO35:
      100 kΩ (top) + 22 kΩ (bottom), with a 100 nF capacitor close to the pin
- [ ] Several I2C connectors, for more sensors and accessories
- [ ] (from the known issues) Bulk capacitor at the servo, or a separate 5 V supply for it
- [ ] (from the known issues) More spare GPIOs on connectors
- [ ] (from the known issues) Charging in place, without unplugging the battery

## Mechanical (FreeCAD)

- [ ] Left panel: move the switch hole 3–4 mm forward and up
- [ ] Left and right panels: remove the extra screw holes, front and back
- [ ] MPU bridge: smaller screws
- [ ] Front face: add a mount for the distance sensor (VL53L0X)
