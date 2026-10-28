#include "probe_gatt.h"
#include "dbt_att_bearer.h"
#include "esp_heap_caps.h"

/* Diagnostic echo/counter service. This is not the product control/OTA profile. */
static dbt_att_server_t server;
static dbt_att_bearer_t bearer;
static dbt_gap_t *gap;
static uint8_t *echo;
static uint16_t echo_size;
static uint32_t writes, notified[DBT_MAX_LINKS], notified_generation[DBT_MAX_LINKS];
static const uint8_t primary[] = {0, 0x28}, declaration[] = {3, 0x28}, cccd[] = {2, 0x29};
static const uint8_t extended_properties[] = {0, 0x29}, reliable_write[] = {1, 0};
static const uint8_t gap_uuid[] = {0, 0x18}, gatt_uuid[] = {1, 0x18};
static const uint8_t name_uuid[] = {0, 0x2a}, changed_uuid[] = {5, 0x2a};
static const uint8_t name[] = "NATIVE_BT_PROBE";
static const uint8_t name_declaration[] = {2, 3, 0, 0, 0x2a};
static const uint8_t changed_declaration[] = {0x20, 6, 0, 5, 0x2a};
static const uint8_t changed_value[] = {1, 0, 0xff, 0xff};
/* 12345678-1234-1234-1234-12345678f000, ...f001, ...f002. */
static const uint8_t service_uuid[] = {0,    0xf0, 0x78, 0x56, 0x34, 0x12, 0x34, 0x12,
                                       0x34, 0x12, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12};
static const uint8_t echo_uuid[] = {1,    0xf0, 0x78, 0x56, 0x34, 0x12, 0x34, 0x12,
                                    0x34, 0x12, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12};
static const uint8_t counter_uuid[] = {2,    0xf0, 0x78, 0x56, 0x34, 0x12, 0x34, 0x12,
                                       0x34, 0x12, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12};
static const uint8_t echo_declaration[] = {0x8e, 0x12, 0,    1,    0xf0, 0x78, 0x56,
                                           0x34, 0x12, 0x34, 0x12, 0x34, 0x12, 0x34,
                                           0x12, 0x78, 0x56, 0x34, 0x12};
static const uint8_t counter_declaration[] = {0x12, 0x15, 0,    2,    0xf0, 0x78, 0x56,
                                              0x34, 0x12, 0x34, 0x12, 0x34, 0x12, 0x34,
                                              0x12, 0x78, 0x56, 0x34, 0x12};
static const dbt_attribute_t attributes[] = {
    {1, DBT_ATT_READ, 2, 2, 0, 2, 0, primary, gap_uuid},
    {2, DBT_ATT_READ, 5, 5, 0, 2, 0, declaration, name_declaration},
    {3, DBT_ATT_READ, sizeof(name) - 1, sizeof(name) - 1, 0, 2, 0, name_uuid, name},
    {4, DBT_ATT_READ, 2, 2, 0, 2, 0, primary, gatt_uuid},
    {5, DBT_ATT_READ, 5, 5, 0, 2, 0, declaration, changed_declaration},
    {6, DBT_ATT_INDICATE, 4, 4, 0, 2, 0, changed_uuid, changed_value},
    {7, DBT_ATT_READ | DBT_ATT_WRITE, 2, 2, 6, 2, 0, cccd, 0},
    {0x10, DBT_ATT_READ, 16, 16, 0, 2, 0, primary, service_uuid},
    {0x11, DBT_ATT_READ, 19, 19, 0, 2, 0, declaration, echo_declaration},
    {0x12, DBT_ATT_READ | DBT_ATT_WRITE | DBT_ATT_DYNAMIC, 512, 0, 0, 16, 0, echo_uuid, 0},
    {0x13, DBT_ATT_READ, 2, 2, 0, 2, 0, extended_properties, reliable_write},
    {0x14, DBT_ATT_READ, 19, 19, 0, 2, 0, declaration, counter_declaration},
    {0x15, DBT_ATT_READ | DBT_ATT_NOTIFY | DBT_ATT_DYNAMIC, 8, 0, 0, 16, 0, counter_uuid, 0},
    {0x16, DBT_ATT_READ | DBT_ATT_WRITE, 2, 2, 0x15, 2, 0, cccd, 0}};
static void copy(uint8_t *out, const uint8_t *in, unsigned size) {
    for (unsigned i = 0; i < size; ++i)
        out[i] = in[i];
}
static void put16(uint8_t *out, uint16_t value) {
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
}
static void counter(uint8_t out[8]) {
    for (unsigned i = 0; i < 4; ++i)
        out[i] = (uint8_t)(writes >> (8u * i));
    put16(out + 4, echo_size);
    put16(out + 6, 1); /* Diagnostic protocol version. */
}
static uint8_t *allocate(void *context, uint16_t size) {
    (void)context;
    return heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}
static void release(void *context, uint8_t *bytes) {
    (void)context;
    heap_caps_free(bytes);
}
static uint8_t read(void *context, const dbt_link_t *link, const dbt_attribute_t *attribute,
                    uint16_t offset, uint8_t *out, uint16_t capacity, uint16_t *written,
                    uint16_t *total) {
    (void)context;
    (void)link;
    uint8_t status[8];
    const uint8_t *source = echo;
    *total = echo_size;
    if (attribute->handle == 0x15) {
        counter(status);
        source = status;
        *total = 8;
    }
    if (offset > *total)
        return DBT_ATT_INVALID_OFFSET;
    *written = (uint16_t)(*total - offset);
    if (*written > capacity)
        *written = capacity;
    if (*written)
        copy(out, source + offset, *written);
    return 0;
}
static uint16_t write(void *context, const dbt_link_t *link, const dbt_att_batch_t *batch,
                      uint32_t token, uint16_t *error_handle) {
    (void)context;
    (void)link;
    (void)token;
    bool changed = false;
    uint16_t size = batch->prepared ? echo_size : 0;
    for (unsigned i = 0; i < batch->count; ++i) {
        const dbt_att_write_t *w = &batch->writes[i];
        if (w->handle != 0x12)
            continue;
        changed = true;
        *error_handle = w->handle;
        if (w->offset > size || (uint32_t)w->offset + w->size > 512)
            return DBT_ATT_INVALID_OFFSET;
        if (w->offset + w->size > size)
            size = (uint16_t)(w->offset + w->size);
    }
    uint8_t *candidate = 0;
    if (changed && size) {
        candidate = allocate(0, size);
        if (!candidate)
            return DBT_ATT_RESOURCES;
        for (unsigned i = 0; i < size; ++i)
            candidate[i] = i < echo_size ? echo[i] : 0;
        for (unsigned i = 0; i < batch->count; ++i) {
            const dbt_att_write_t *w = &batch->writes[i];
            if (w->handle == 0x12)
                copy(candidate + w->offset, w->data, w->size);
        }
    }
    if (changed) {
        release(0, echo);
        echo = candidate;
        echo_size = size;
    }
    ++writes;
    return 0;
}
static void failed(void *context, uint16_t handle, uint32_t generation) {
    (void)context;
    const dbt_link_t *link = dbt_acl_find(&bearer.runtime->acl, handle);
    if (link && link->generation == generation)
        (void)dbt_gap_disconnect(gap, link, 0x13);
}
dbt_result_t probe_gatt_init(dbt_runtime_t *runtime, dbt_gap_t *configuration) {
    dbt_att_hooks_t hooks = {0};
    hooks.allocate = allocate;
    hooks.release = release;
    hooks.read = read;
    hooks.write = write;
    hooks.failed = failed;
    dbt_result_t result = dbt_att_init(
        &server, attributes, sizeof(attributes) / sizeof(attributes[0]), hooks, 517, 1024);
    if (result != DBT_OK)
        return result;
    gap = configuration;
    return dbt_att_bearer_init(&bearer, runtime, &server);
}
void probe_gatt_advertisement(dbt_gap_config_t *configuration) {
    configuration->advertising[0] = 2;
    configuration->advertising[1] = 1;
    configuration->advertising[2] = 2;
    configuration->advertising[3] = 17;
    configuration->advertising[4] = 7;
    copy(configuration->advertising + 5, service_uuid, 16);
    configuration->advertising_size = 21;
    configuration->scan_response[0] = sizeof(name);
    configuration->scan_response[1] = 9;
    copy(configuration->scan_response + 2, name, sizeof(name) - 1);
    configuration->scan_response_size = sizeof(name) + 1;
}
void probe_gatt_link(void *context, const dbt_link_change_t *change) {
    (void)context;
    if (!bearer.runtime)
        return;
    dbt_result_t result = dbt_att_bearer_link(&bearer, change);
    if (result != DBT_OK)
        dbt_host_abort(&bearer.runtime->host, DBT_FAULT_BAD_RESPONSE);
}
void probe_gatt_receive(void *context, const dbt_link_t *link, uint16_t cid, const uint8_t *data,
                        uint16_t size) {
    (void)context;
    dbt_result_t result = dbt_att_bearer_receive(&bearer, link, cid, data, size);
    if (result == DBT_INVALID || result == DBT_BUSY)
        (void)dbt_gap_disconnect(gap, link, 0x13);
}
void probe_gatt_poll(uint32_t now_ms) {
    if (!bearer.runtime)
        return;
    dbt_att_bearer_poll(&bearer, now_ms);
    for (unsigned i = 0; i < DBT_MAX_LINKS; ++i) {
        const dbt_link_t *link = &bearer.runtime->acl.links[i];
        if (!link->used || link->kind != DBT_LINK_LE)
            continue;
        if (notified_generation[i] != link->generation) {
            notified_generation[i] = link->generation;
            notified[i] = UINT32_MAX;
        }
        if (notified[i] == writes)
            continue;
        uint8_t bytes[8];
        counter(bytes);
        dbt_result_t result = dbt_att_notify(&server, link, 0x15, bytes, 8, false);
        if (result == DBT_OK || result == DBT_UNSUPPORTED)
            notified[i] = writes;
    }
}
uint32_t probe_gatt_wait_ms(uint32_t now_ms) {
    return bearer.runtime ? dbt_att_bearer_wait_ms(&bearer, now_ms) : UINT32_MAX;
}
bool probe_gatt_connected(void) {
    if (!bearer.runtime)
        return false;
    for (unsigned i = 0; i < DBT_MAX_LINKS; ++i)
        if (bearer.runtime->acl.links[i].used && bearer.runtime->acl.links[i].kind == DBT_LINK_LE)
            return true;
    return false;
}
unsigned probe_gatt_bytes(void) { return sizeof(server) + sizeof(bearer); }
