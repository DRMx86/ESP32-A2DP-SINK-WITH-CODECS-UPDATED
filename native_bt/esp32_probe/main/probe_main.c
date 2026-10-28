#include <inttypes.h>
#include <stdio.h>
#include "dbt_gap.h"
#include "esp_bt.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#if !CONFIG_IDF_TARGET_ESP32 || !CONFIG_BT_CONTROLLER_ONLY || !CONFIG_BTDM_CTRL_MODE_BTDM
#error "Probe requires original ESP32, controller-only host selection and dual mode"
#endif

static dbt_runtime_t runtime;
static dbt_gap_t gap;
static TaskHandle_t owner;
static const char *tag = "native_bt_probe";

static uint32_t now_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void wake_owner(void) {
    if (xPortInIsrContext()) {
        BaseType_t higher_priority = pdFALSE;
        vTaskNotifyGiveFromISR(owner, &higher_priority);
        if (higher_priority) portYIELD_FROM_ISR();
    } else {
        xTaskNotifyGive(owner);
    }
}

static void send_available(void) { wake_owner(); }

static int receive_packet(uint8_t *data, uint16_t size) {
    /* No allocation, logging, decoding, profile state mutation or waiting here. */
    (void)dbt_runtime_receive(&runtime, data, size);
    wake_owner();
    return 0; /* VHCI has no supported retry contract; core latches any loss. */
}

static bool transport_ready(void *context) {
    (void)context;
    return esp_vhci_host_check_send_available();
}

static bool transport_send(void *context, uint8_t *data, uint16_t size) {
    (void)context;
    if (!esp_vhci_host_check_send_available()) return false;
    /* Host task only, outside critical sections and scheduler suspension. */
    esp_vhci_host_send_packet(data, size);
    return true;
}

static uint8_t *allocate_rx(void *context, uint16_t size) {
    (void)context;
    return heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}
static void release_rx(void *context, uint8_t *data) {
    (void)context;
    heap_caps_free(data);
}
static void observe(void *context, const dbt_hci_packet_t *packet) {
    (void)context;
    if (packet->type != DBT_H4_EVENT || gap.state == DBT_GAP_IDLE) return;
    dbt_result_t result = dbt_gap_event(&gap, packet);
    if (result == DBT_INVALID || result == DBT_NO_SPACE)
        dbt_host_abort(&runtime.host, result == DBT_NO_SPACE ? DBT_FAULT_RX_OVERRUN : DBT_FAULT_BAD_PACKET);
}

void app_main(void) {
    owner = xTaskGetCurrentTaskHandle();
    vTaskPrioritySet(owner, 6);
    dbt_transport_t transport = { 0, transport_ready, transport_send };
    dbt_runtime_hooks_t hooks = {0};
    hooks.allocate = allocate_rx; hooks.release = release_rx; hooks.packet = observe;
    if (dbt_runtime_init(&runtime, transport, hooks, 4096, 2000) != DBT_OK) {
        ESP_LOGE(tag, "core initialization failed");
        return;
    }
    dbt_gap_config_t gap_config = { .name = "NATIVE_BT_PROBE",
        .advertising = {2, 1, 2}, .advertising_size = 3,
        .advertising_minimum = 0x80, .advertising_maximum = 0x100 };
    dbt_gap_hooks_t gap_hooks = {0};
    if (dbt_gap_init(&gap, &runtime.host, gap_config, gap_hooks) != DBT_OK) {
        ESP_LOGE(tag, "GAP initialization failed");
        return;
    }
    /* Existing NVS is preserved. Initialization failure does not erase bonds/settings. */
    ESP_ERROR_CHECK(nvs_flash_init());
    esp_bt_controller_config_t configuration = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&configuration));
    static const esp_vhci_host_callback_t callbacks = { send_available, receive_packet };
    ESP_ERROR_CHECK(esp_vhci_host_register_callback(&callbacks));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BTDM));
    if (dbt_runtime_start(&runtime, now_ms()) != DBT_OK) {
        ESP_LOGE(tag, "core start failed");
        return;
    }
    dbt_host_t *host = &runtime.host;
    dbt_host_state_t previous = DBT_HOST_IDLE;
    dbt_gap_state_t previous_gap = DBT_GAP_IDLE;
    for (;;) {
        uint32_t now = now_ms();
        dbt_runtime_poll(&runtime, now, 8, 8);
        if (runtime.ready && gap.state == DBT_GAP_IDLE) (void)dbt_gap_start(&gap);
        dbt_gap_poll(&gap, now);
        if (host->state != previous) {
            previous = host->state;
            if (host->state == DBT_HOST_IDENTIFIED) {
                printf("{\"probe\":\"controller_identified\",\"profiles_ready\":false,"
                       "\"hci_version\":%u,\"manufacturer\":%u,\"host_bytes\":%u,\"runtime_bytes\":%u,"
                       "\"classic_acl_bytes\":%u,\"classic_acl_packets\":%u,"
                       "\"le_acl_bytes\":%u,\"le_acl_packets\":%u,\"shared_acl_pool\":%s,"
                       "\"internal_free\":%u,\"internal_largest\":%u}\n",
                       host->controller.hci_version, host->controller.manufacturer,
                       (unsigned)sizeof(*host), (unsigned)sizeof(runtime), host->controller.classic_acl_bytes,
                       host->controller.classic_acl_packets, host->controller.le_acl_bytes,
                       host->controller.le_acl_packets, host->controller.shared_acl_pool ? "true" : "false",
                       (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                       (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
            } else if (host->state == DBT_HOST_FAILED) {
                printf("{\"probe\":\"failed\",\"fault\":%u,\"controller_status\":%u,"
                       "\"pending_opcode\":%u,\"rx_rejected\":%" PRIu32 "}\n",
                       (unsigned)host->fault, host->controller_status, host->pending_opcode,
                       atomic_load_explicit(&host->ingress.rejected, memory_order_relaxed));
            }
        }
        if (gap.state != previous_gap) {
            previous_gap = gap.state;
            printf("{\"probe\":\"gap_state\",\"state\":%u,\"controller_status\":%u,"
                   "\"profiles_ready\":false,\"advertising\":false,\"classic_scan\":0}\n",
                   (unsigned)gap.state, gap.last_status);
        }
        uint32_t wait = dbt_runtime_wait_ms(&runtime, now_ms());
        uint32_t gap_wait = dbt_gap_wait_ms(&gap);
        if (gap_wait < wait) wait = gap_wait;
        if (wait == 0) continue;
        TickType_t ticks = portMAX_DELAY;
        if (wait != UINT32_MAX) {
            ticks = pdMS_TO_TICKS(wait);
            if (ticks == 0) ticks = 1;
        }
        /* A notification before this call remains pending, avoiding a lost wakeup. */
        ulTaskNotifyTake(pdTRUE, ticks);
    }
}
