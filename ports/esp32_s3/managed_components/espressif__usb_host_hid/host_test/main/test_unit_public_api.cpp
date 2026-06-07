/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <catch2/catch_test_macros.hpp>

#include "usb/hid_host.h"
#include "usb/hid.h"

extern "C" {
#include "Mockusb_host.h"
#include "Mockqueue.h"
#include "Mocktask.h"
#include "Mockidf_additions.h"
#include "Mockportmacro.h"
}

SCENARIO("HID Host install")
{
    hid_host_driver_config_t hid_host_driver_config = {
        .create_background_task = true,
        .task_priority = 5,
        .stack_size = 4096,
        .core_id = 0,
        .callback = (reinterpret_cast<hid_host_driver_event_cb_t>(0xdeadbeef)),
        .callback_arg = nullptr
    };

    // HID Host driver config set to nullptr
    GIVEN("NO HID Host driver config, driver not already installed") {

        // Call hid_host_install with hid_host_driver_config set to nullptr
        SECTION("Config is nullptr") {
            REQUIRE(ESP_ERR_INVALID_ARG == hid_host_install(nullptr));
        }
    }

    // HID Host driver config set to config from HID Host example, but without callback
    GIVEN("Minimal HID Host driver config, driver not already installed") {
        hid_host_driver_config.callback = nullptr;

        SECTION("Config error: no callback") {
            // Call the DUT function, expect ESP_ERR_INVALID_ARG
            REQUIRE(ESP_ERR_INVALID_ARG == hid_host_install(&hid_host_driver_config));
        }

        SECTION("Config error: stack size is 0") {
            hid_host_driver_config.stack_size = 0;
            // Call the DUT function, expect ESP_ERR_INVALID_ARG
            REQUIRE(ESP_ERR_INVALID_ARG == hid_host_install(&hid_host_driver_config));
        }

        SECTION("Config error: task priority is 0") {
            hid_host_driver_config.task_priority = 0;
            // Call the DUT function, expect ESP_ERR_INVALID_ARG
            REQUIRE(ESP_ERR_INVALID_ARG == hid_host_install(&hid_host_driver_config));
        }
    }

    // HID Host driver config set to config from HID Host example
    GIVEN("Full HID Host config, driver not already installed") {
        int mtx;
        int open_close_mtx;

        // Install error: failed to create semaphore
        SECTION("Install error: unable to create semaphore") {
            // We must call xQueueGenericCreate_ExpectAnyArgsAndReturn instead of xSemaphoreCreateBinary_ExpectAnyArgsAndReturn
            // Because of missing Freertos Mocks
            // Create a semaphore, return nullptr, so the semaphore is not created successfully
            xQueueGenericCreate_ExpectAnyArgsAndReturn(nullptr);

            // Call the DUT function, expect ESP_ERR_NO_MEM
            REQUIRE(ESP_ERR_NO_MEM == hid_host_install(&hid_host_driver_config));
        }

        // Unable to register client: Invalid state, goto fail
        SECTION("Client register not successful: goto fail") {
            // We must call xQueueGenericCreate_ExpectAnyArgsAndReturn instead of xSemaphoreCreateBinary_ExpectAnyArgsAndReturn
            // Because of missing Freertos Mocks
            // Create a semaphore, return the semaphore handle (Queue Handle in this scenario), so the semaphore is created successfully
            xQueueGenericCreate_ExpectAnyArgsAndReturn(reinterpret_cast<QueueHandle_t>(&mtx));
            xQueueCreateMutexStatic_ExpectAnyArgsAndReturn(reinterpret_cast<SemaphoreHandle_t>(&open_close_mtx)); // Static calls should always succeed, no need to mock failure
            // Register a client, return ESP_ERR_INVALID_STATE, so the client is not registered successfully
            usb_host_client_register_ExpectAnyArgsAndReturn(ESP_ERR_INVALID_STATE);

            // goto fail: delete the semaphore (Queue in this scenario)
            vQueueDelete_Expect(reinterpret_cast<QueueHandle_t>(&mtx));

            // Call the DUT function, expect ESP_ERR_INVALID_STATE
            REQUIRE(ESP_ERR_INVALID_STATE == hid_host_install(&hid_host_driver_config));
        }

        SECTION("Client register successful: create background task fail") {
            usb_host_client_handle_t client_handle;

            // Create a semaphore, return the semaphore handle (Queue Handle in this scenario), so the semaphore is created successfully
            xQueueGenericCreate_ExpectAnyArgsAndReturn(reinterpret_cast<QueueHandle_t>(&mtx));
            xQueueCreateMutexStatic_ExpectAnyArgsAndReturn(reinterpret_cast<SemaphoreHandle_t>(&open_close_mtx)); // Static calls should always succeed, no need to mock failure
            // Register a client, return ESP_OK, so the client is registered successfully
            usb_host_client_register_ExpectAnyArgsAndReturn(ESP_OK);
            // Fill the pointer to the client_handle, which is used in goto fail, to deregister the client
            usb_host_client_register_ReturnThruPtr_client_hdl_ret(&client_handle);

            // Create a background task, return pdFALSE, so the task in not created successfully
            vPortEnterCritical_Expect();
            vPortExitCritical_Expect();
            xTaskCreatePinnedToCore_ExpectAnyArgsAndReturn(pdFALSE);

            // goto fail: delete the semaphore and deregister the client
            // usb_host_client_deregister is not being checked for the return value !! Consider adding it to the host driver
            usb_host_client_deregister_ExpectAndReturn(client_handle, ESP_OK);
            // Delete the semaphore (Queue in this scenario)
            vQueueDelete_Expect(reinterpret_cast<QueueHandle_t>(&mtx));

            // Call the DUT function, expect ESP_ERR_NO_MEM
            REQUIRE(ESP_ERR_NO_MEM == hid_host_install(&hid_host_driver_config));
        }

        // Call hid_host_install and expect successful installation
        SECTION("Client register successful: hid_host_install successful") {
            // Create semaphore, return semaphore handle (Queue Handle in this scenario), so the semaphore is created successfully
            xQueueGenericCreate_ExpectAnyArgsAndReturn(reinterpret_cast<QueueHandle_t>(&mtx));
            xQueueCreateMutexStatic_ExpectAnyArgsAndReturn(reinterpret_cast<SemaphoreHandle_t>(&open_close_mtx));
            // return ESP_OK, so the client is registered successfully
            usb_host_client_register_ExpectAnyArgsAndReturn(ESP_OK);

            // Create a background task successfully by returning pdTRUE
            vPortEnterCritical_Expect();
            vPortExitCritical_Expect();
            xTaskCreatePinnedToCore_ExpectAnyArgsAndReturn(pdTRUE);

            // Call the DUT function, expect ESP_OK
            REQUIRE(ESP_OK == hid_host_install(&hid_host_driver_config));
        }
    }

    // HID Host driver config set to config from HID Host example
    GIVEN("Full HID Host config, driver already installed") {

        // Driver is already installed
        SECTION("Install error: driver already installed") {
            // Call the DUT function again to get the ESP_ERR_INVALID_STATE error
            REQUIRE(ESP_ERR_INVALID_STATE == hid_host_install(&hid_host_driver_config));
        }
    }
}
