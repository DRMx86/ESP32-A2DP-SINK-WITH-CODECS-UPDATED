#ifndef PROBE_GATT_H
#define PROBE_GATT_H
#include "dbt_gap.h"

dbt_result_t probe_gatt_init(dbt_runtime_t *runtime, dbt_gap_t *gap);
void probe_gatt_advertisement(dbt_gap_config_t *configuration);
void probe_gatt_link(void *context, const dbt_link_change_t *change);
void probe_gatt_receive(void *context, const dbt_link_t *link, uint16_t cid, const uint8_t *data,
                        uint16_t size);
void probe_gatt_poll(uint32_t now_ms);
uint32_t probe_gatt_wait_ms(uint32_t now_ms);
bool probe_gatt_connected(void);
unsigned probe_gatt_bytes(void);
#endif
