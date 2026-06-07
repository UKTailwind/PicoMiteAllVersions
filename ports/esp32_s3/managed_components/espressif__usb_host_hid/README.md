# USB Host HID (Human Interface Device) Driver

[![Component Registry](https://components.espressif.com/components/espressif/usb_host_hid/badge.svg)](https://components.espressif.com/components/espressif/usb_host_hid) ![maintenance-status](https://img.shields.io/badge/maintenance-passively--maintained-yellowgreen.svg) ![changelog](https://img.shields.io/badge/Keep_a_Changelog-blue?logo=keepachangelog&logoColor=E05735)

This directory contains an implementation of a USB HID Driver implemented on top of the [USB Host Library](https://components.espressif.com/components/espressif/usb).

HID driver allows access to HID devices.

## Usage

The following steps outline the typical API call pattern of the HID Class Driver:

1. Install the USB Host Library via 'usb_host_install()'

2. Install the HID driver via 'hid_host_install()'

3. The HID Host driver device callback provide the following events (via two callbacks):

   - HID_HOST_DRIVER_EVENT_CONNECTED
   - HID_HOST_INTERFACE_EVENT_INPUT_REPORT
   - HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR
   - HID_HOST_INTERFACE_EVENT_DISCONNECTED

4. Specific HID device can be opened or closed with:

   - 'hid_host_device_open()'
   - 'hid_host_device_close()'

5. To enable / disable data receiving in case of event (keyboard key was pressed or mouse device was moved e.t.c) use:

   - 'hid_host_device_start()'
   - 'hid_host_device_stop()'

6. HID Class specific device requests:

   - 'hid_host_interface_get_report_descriptor()'
   - 'hid_class_request_get_report()'
   - 'hid_class_request_get_idle()'
   - 'hid_class_request_get_protocol()'
   - 'hid_class_request_set_report()'
   - 'hid_class_request_set_idle()'
   - 'hid_class_request_set_protocol()'

7. When HID device event occurs the driver call an interface callback with events:

   - HID_HOST_INTERFACE_EVENT_INPUT_REPORT
   - HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR
   - HID_HOST_INTERFACE_EVENT_DISCONNECTED

8. The HID driver can be uninstalled via 'hid_host_uninstall()'

## Known issues

- Empty

## Examples

- For an example, refer to [hid_host_example](https://github.com/espressif/esp-idf/tree/master/examples/peripherals/usb/host/hid)

## Supported Devices

- HID Driver support any HID compatible device with a USB bIterfaceClass 0x03 (Human Interface Device).
- There are two options to handle HID device input data: either in RAW format or via special event handlers (which are available only for HID Devices which support Boot Protocol).
