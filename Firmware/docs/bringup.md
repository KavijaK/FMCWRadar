# Bring-Up Checklist

1. Verify HSE bypass operation and PLL: `SYSCLK=168 MHz`, `APB1=42 MHz`, `APB2=84 MHz`.
2. Scope PA8. It should output `10.000000 MHz` from `MCO1 = HSE / 2`.
3. Confirm USB3317 26 MHz reference and ULPI 60 MHz clock before enumeration.
4. Enumerate USB. The device should expose one vendor-specific interface and one bulk IN endpoint.
5. Validate DCMI packing with a known digital pattern before enabling the RF ramp.
6. Confirm PE5 MUXOUT pulses every 1 ms from the ADF4158.
7. Scope PA4 during first MUXOUT. It should rise about 309 ns after the first edge.
8. Check PA7 at the same time. It should follow PA4 because it is the routed-back HSYNC feedback signal.
9. Confirm captured ADC data looks correct with DCMI sampling on the falling PIXCLK edge.
10. Run `python host/fmcw_capture.py --frames 1000 --stats`.
11. Watch for nonzero flags. `USB_LATE` means the host is not draining quickly enough; `DROPPED`, `DCMI_OVR`, or `ALIGN_LOST` means discard affected data.
