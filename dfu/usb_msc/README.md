# USB MSC (internal flash)

This sample exposes a safe internal flash partition (`slot1_partition` / `image-1`) as a USB Mass Storage (MSC) disk using Zephyr's "next" USB device stack.

## Build (nRF5340DK app core)

```sh
west build -p -b nrf5340dk_nrf5340_cpuapp .
```

## Notes

- The backing store is `slot1_partition` (MCUboot secondary image slot). On nRF5340DK this is typically ~464KB.
- This is a raw block device export; the host may ask to format it.
