# SPDX-FileCopyrightText: 2024-2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0

from typing import Tuple

import pytest
from pytest_embedded_idf.dut import IdfDut


@pytest.mark.usb_host
@pytest.mark.parametrize(
    'count, config, target',
    [
        pytest.param(2, 'default', 'esp32s2'),
        pytest.param(2, 'default', 'esp32s3'),
        pytest.param(2, 'default', 'esp32p4', marks=[pytest.mark.eco_default]),
        pytest.param(2, 'esp32p4_eco4', 'esp32p4', marks=[pytest.mark.esp32p4_eco4]),
    ],
    indirect=True,
)
def test_usb_host_hid(dut: Tuple[IdfDut, IdfDut]) -> None:
    device = dut[0]
    host = dut[1]

    hid_dev = "HID mock device with"
    tests = [
        ("hid_device",                     ["hid_host", "hid_host_pm"],     f"{hid_dev} 1xInterface (Protocol=None) has been started"),
        ("hid_device_two_ifaces",          ["hid_host", "hid_host_pm"],     f"{hid_dev} 2xInterfaces (Protocol=BootKeyboard, Protocol=BootMouse) has been started"),
        ("hid_device_large_report",        ["hid_host"],                    f"{hid_dev} large report descriptor has been started"),
        ("hid_device_extra_large_report",  ["hid_host_extra_large_report"], f"{hid_dev} extra large report descriptor has been started"),
        ("hid_device_suspend_dconn",       ["hid_host_suspend_dconn"],      f"{hid_dev} 2xInterfaces (Protocol=BootKeyboard, Protocol=BootMouse) has been started"),
        ("hid_device_resume_dconn",        ["hid_host_resume_dconn"],       f"{hid_dev} 2xInterfaces (Protocol=BootKeyboard, Protocol=BootMouse) has been started"),
        ("hid_device_remote_wake",         ["hid_host_remote_wake"],        f"{hid_dev} 2xInterfaces (Protocol=BootKeyboard, Protocol=BootMouse) has been started"),
    ]

    for dev_test_mode, host_test_case_groups, device_expect_string in tests:
        # Prepare USB Device for test
        device.expect_exact("Press ENTER to see the list of tests.")
        device.write(f"[{dev_test_mode}]")
        device.expect_exact({device_expect_string})

        # Run tests USB Host tests
        for host_test_case_group in host_test_case_groups:
            host.run_all_single_board_cases(group=host_test_case_group)
        device.serial.hard_reset()
