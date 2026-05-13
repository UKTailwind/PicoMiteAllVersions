/*
 * pico_gpio_irq.c - RAM-resident GPIO IRQ dispatcher for Pico SDK ports.
 *
 * Replaces the SDK's gpio_default_irq_handler (which lives in flash)
 * with our own that lives in SRAM. MMBasic writes to flash at runtime
 * (SAVE, firmware update), during which XIP is disabled and any
 * flash-resident interrupt handler becomes unreachable — a GPIO IRQ
 * firing at that moment would hang the chip.
 *
 * Historically this was solved by patching the SDK's gpio.c to wrap
 * gpio_default_irq_handler with __not_in_flash_func(). We replace that
 * with a drop-in that registers itself alongside any other shared
 * handlers on IO_IRQ_BANK0 (notably cyw43's raw handler on WEB/WEBRP2350
 * builds), so the SDK tree can stay stock.
 *
 * Coexistence model — shared handler, not exclusive:
 *   - We install via irq_add_shared_handler() at
 *     GPIO_IRQ_CALLBACK_ORDER_PRIORITY (same slot the SDK default would
 *     use). This permits cyw43 (and any other SDK code that hooks
 *     IO_IRQ_BANK0 via gpio_add_raw_irq_handler_*) to register its own
 *     shared handler without assertion failure.
 *   - Our dispatcher only ack's and dispatches events on pins we
 *     registered via pico_gpio_irq_set_enabled(). Events on other
 *     pins (e.g. CYW43_PIN_WL_HOST_WAKE) are left for their own
 *     shared handler to process.
 *
 * Simplifications vs. the SDK default:
 *   - Single global callback (External.c:gpio_callback) - no per-core
 *     callbacks[] array.
 *   - Dispatcher installed once globally, then just flip per-pin bits.
 *     MMBasic registers IRQs only from core 0 in practice, so a
 *     single-core installation is sufficient; if that assumption ever
 *     changes, revisit the install logic.
 */

#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/structs/io_bank0.h"
#include "pico/platform.h"

#include "pico_gpio_irq.h"

/* Real definition lives in External.c. Forward-declared here so we
 * don't drag External.h's MMBasic type dependencies into this TU. */
extern void gpio_callback(unsigned int gpio, uint32_t events);

/* Bitmask of GPIO pins registered through our API. Pins set here will
 * be ack'd and dispatched by pico_gpio_irq_dispatch; pins not set
 * are left alone (their events belong to some other shared handler). */
static volatile uint64_t pico_gpio_registered_mask = 0;

/* Whether our shared handler is installed on IO_IRQ_BANK0 yet. */
static volatile bool dispatch_installed = false;

static void __not_in_flash_func(pico_gpio_irq_dispatch)(void) {
    uint64_t mask = pico_gpio_registered_mask;
    io_bank0_irq_ctrl_hw_t *ctrl = get_core_num()
        ? &io_bank0_hw->proc1_irq_ctrl
        : &io_bank0_hw->proc0_irq_ctrl;
    for (uint gpio = 0; gpio < NUM_BANK0_GPIOS; gpio += 8) {
        uint32_t events8 = ctrl->ints[gpio >> 3u];
        for (uint i = gpio; events8 && i < gpio + 8; i++) {
            uint32_t events = events8 & 0xfu;
            if (events && (mask & (1ull << i))) {
                gpio_acknowledge_irq(i, events);
                gpio_callback(i, events);
            }
            events8 >>= 4;
        }
    }
}

void __not_in_flash_func(pico_gpio_irq_set_enabled)(unsigned int gpio,
                                                    uint32_t events,
                                                    bool enabled) {
    /* Install our shared handler the first time anyone registers a
     * pin. Guarded against a two-core race even though PicoMite
     * registers from core 0 in practice. */
    if (!dispatch_installed) {
        uint32_t save = save_and_disable_interrupts();
        if (!dispatch_installed) {
            irq_add_shared_handler(IO_IRQ_BANK0, pico_gpio_irq_dispatch,
                                   GPIO_IRQ_CALLBACK_ORDER_PRIORITY);
            dispatch_installed = true;
        }
        restore_interrupts(save);
    }

    /* Track which pins are "ours" so the dispatcher knows what to
     * ack/dispatch vs. what to leave for other handlers. Update is
     * atomic wrt. IRQs via the save_and_disable guard. */
    uint32_t save = save_and_disable_interrupts();
    if (enabled) pico_gpio_registered_mask |=  (1ull << gpio);
    else         pico_gpio_registered_mask &= ~(1ull << gpio);
    restore_interrupts(save);

    gpio_set_irq_enabled(gpio, events, enabled);
    if (enabled) irq_set_enabled(IO_IRQ_BANK0, true);
}
