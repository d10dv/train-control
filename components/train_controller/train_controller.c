#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "esp_log.h"

#include "debug_log.h"
#include "event_bus.h"
#include "mqtt_broker.h"
#include "display.h"
#include "train_controller.h"

static const char *TAG = "train_ctrl";

#define MAX_TRAINS      8
#define TRAIN_ID_MAXLEN 16
#define SPEED_STEP      5
#define SPEED_MAX       100

typedef struct {
    char id[TRAIN_ID_MAXLEN];
    char model[16];
    bool online;
    int  speed;         /* -100 .. +100, sign = direction */
    bool lights_on;
} train_t;

static train_t s_trains[MAX_TRAINS];
static int     s_train_count = 0;
static int     s_active_idx  = -1;

/* ─── Helpers ─── */

static int find_train(const char *id)
{
    for (int i = 0; i < s_train_count; i++) {
        if (strcmp(s_trains[i].id, id) == 0)
            return i;
    }
    return -1;
}

static int add_train(const char *id, const char *model)
{
    if (s_train_count >= MAX_TRAINS) {
        ESP_LOGW(TAG, "train list full, ignoring %s", id);
        return -1;
    }
    train_t *t = &s_trains[s_train_count];
    memset(t, 0, sizeof(*t));
    strlcpy(t->id, id, sizeof(t->id));
    if (model)
        strlcpy(t->model, model, sizeof(t->model));
    t->online = true;
    ESP_LOGI(TAG, "discovered train \"%s\" [%d/%d]", t->id, s_train_count + 1, MAX_TRAINS);
    return s_train_count++;
}

/* ─── MQTT command publishing ─── */

static void publish_cmd(const char *train_id, const char *json, size_t len)
{
    char topic[64];
    snprintf(topic, sizeof(topic), "train/%s/cmd", train_id);
    mqtt_broker_publish_internal(topic, json, len);
}

static void publish_speed(const train_t *t)
{
    char buf[48];
    int n = snprintf(buf, sizeof(buf),
                     "{\"action\":\"speed\",\"value\":%d}", abs(t->speed));
    publish_cmd(t->id, buf, n);
}

static void publish_direction(const train_t *t)
{
    char buf[48];
    int n = snprintf(buf, sizeof(buf),
                     "{\"action\":\"direction\",\"value\":\"%s\"}",
                     t->speed >= 0 ? "forward" : "reverse");
    publish_cmd(t->id, buf, n);
}

static void publish_stop(const train_t *t)
{
    const char *json = "{\"action\":\"stop\"}";
    publish_cmd(t->id, json, strlen(json));
}

static void publish_estop(const train_t *t)
{
    const char *json = "{\"action\":\"estop\"}";
    publish_cmd(t->id, json, strlen(json));
}

static void publish_horn(const train_t *t, const char *value)
{
    char buf[48];
    int n = snprintf(buf, sizeof(buf),
                     "{\"action\":\"horn\",\"value\":\"%s\"}", value);
    publish_cmd(t->id, buf, n);
}

static void publish_light(const train_t *t, bool on)
{
    char buf[64];
    int n = snprintf(buf, sizeof(buf),
                     "{\"action\":\"light\",\"target\":\"all\",\"value\":%s}",
                     on ? "true" : "false");
    publish_cmd(t->id, buf, n);
}

/* ─── Display ─── */

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"

static void update_display(void)
{
    char line[9]; /* 8 chars + NUL (2x font: 8 columns on 128px) */

    if (s_active_idx < 0 || s_train_count == 0) {
        display_draw_text_2x(0, 0, "No train");
        display_draw_text_2x(0, 1, "        ");
        return;
    }

    train_t *t = &s_trains[s_active_idx];

    /* Row 0: train id (truncated to 8 chars) */
    snprintf(line, sizeof(line), "%-8.8s", t->id);
    display_draw_text_2x(0, 0, line);

    /* Row 1: speed + direction + index */
    const char *dir = t->speed > 0 ? "F" : (t->speed < 0 ? "R" : "-");
    snprintf(line, sizeof(line), "%3d%s %d/%d",
             abs(t->speed), dir,
             s_active_idx + 1, s_train_count);
    display_draw_text_2x(0, 1, line);
}

#pragma GCC diagnostic pop

/* ─── Speed change with direction crossing logic ─── */

static void handle_speed_change(train_t *t, int old_speed)
{
    bool dir_changed = (old_speed >= 0 && t->speed < 0) ||
                       (old_speed <= 0 && t->speed > 0);

    if (dir_changed) {
        publish_stop(t);
        publish_direction(t);
    }
    publish_speed(t);
    update_display();
}

/* ─── Active train cycling ─── */

static void cycle_active_train(void)
{
    if (s_train_count == 0) {
        s_active_idx = -1;
        return;
    }

    /* Find next online train starting from current + 1 */
    for (int i = 1; i <= s_train_count; i++) {
        int idx = (s_active_idx + i) % s_train_count;
        if (s_trains[idx].online) {
            s_active_idx = idx;
            ESP_LOGI(TAG, "active train: \"%s\"", s_trains[idx].id);
            return;
        }
    }
    /* No online trains found */
    s_active_idx = -1;
}

/* ─── Simple JSON string field extractor ─── */

static bool extract_json_string(const char *json, const char *key,
                                char *out, size_t out_size)
{
    char pattern[32];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) return false;

    p += strlen(pattern);
    /* skip optional whitespace and colon */
    while (*p == ' ' || *p == ':') p++;
    if (*p != '"') return false;
    p++; /* skip opening quote */

    size_t i = 0;
    while (*p && *p != '"' && i < out_size - 1) {
        out[i++] = *p++;
    }
    out[i] = '\0';
    return i > 0;
}

/* ─── Event handlers ─── */

static void on_mqtt_message(void *arg, esp_event_base_t base,
                            int32_t event_id, void *event_data)
{
    mqtt_message_event_t *evt = (mqtt_message_event_t *)event_data;

    if (strcmp(evt->topic, "train/announce") != 0)
        return;

    /* Simple JSON field extraction from announce payload.
     * Expected: {"id":"loco1","model":"steam","fw":"1.0.3"}
     * LWT adds: "online":false */
    char id[TRAIN_ID_MAXLEN] = {0};
    char model[16] = {0};
    bool online = true;

    if (!extract_json_string(evt->payload, "id", id, sizeof(id))) {
        ESP_LOGW(TAG, "announce missing 'id'");
        return;
    }
    extract_json_string(evt->payload, "model", model, sizeof(model));

    /* Check for "online":false (LWT) */
    const char *online_ptr = strstr(evt->payload, "\"online\"");
    if (online_ptr && strstr(online_ptr, "false"))
        online = false;

    int idx = find_train(id);
    if (idx >= 0) {
        s_trains[idx].online = online;
        ESP_LOGI(TAG, "train \"%s\" %s", id, online ? "online" : "offline");
    } else if (online) {
        idx = add_train(id, model);
    }

    /* Auto-select first online train */
    if (s_active_idx < 0 && idx >= 0 && s_trains[idx].online) {
        s_active_idx = idx;
        ESP_LOGI(TAG, "auto-selected train \"%s\"", s_trains[idx].id);
    }

    /* If active train went offline, try to find another */
    if (s_active_idx >= 0 && !s_trains[s_active_idx].online) {
        cycle_active_train();
    }

    update_display();
}

static void on_encoder_event(void *arg, esp_event_base_t base,
                             int32_t event_id, void *event_data)
{
    if (s_active_idx < 0)
        return;

    train_t *t = &s_trains[s_active_idx];

    switch (event_id) {
    case TRAIN_EVT_ENCODER_CW: {
        int old = t->speed;
        t->speed += SPEED_STEP;
        if (t->speed > SPEED_MAX) t->speed = SPEED_MAX;
        handle_speed_change(t, old);
        break;
    }
    case TRAIN_EVT_ENCODER_CCW: {
        int old = t->speed;
        t->speed -= SPEED_STEP;
        if (t->speed < -SPEED_MAX) t->speed = -SPEED_MAX;
        handle_speed_change(t, old);
        break;
    }
    case TRAIN_EVT_ENCODER_CLICK:
        cycle_active_train();
        update_display();
        break;
    default:
        break;
    }
}

static void on_button_event(void *arg, esp_event_base_t base,
                            int32_t event_id, void *event_data)
{
    if (s_active_idx < 0)
        return;

    input_event_t *evt = (input_event_t *)event_data;
    train_t *t = &s_trains[s_active_idx];

    if (evt->input_id == 0) {
        if (event_id == TRAIN_EVT_BUTTON_PRESS) {
            publish_stop(t);
            t->speed = 0;
            update_display();
        } else if (event_id == TRAIN_EVT_BUTTON_LONG_PRESS) {
            publish_estop(t);
            t->speed = 0;
            update_display();
        }
    } else if (evt->input_id == 1) {
        if (event_id == TRAIN_EVT_BUTTON_PRESS) {
            publish_horn(t, "short");
        } else if (event_id == TRAIN_EVT_BUTTON_LONG_PRESS) {
            t->lights_on = !t->lights_on;
            publish_light(t, t->lights_on);
        }
    }
}

/* ─── Init ─── */

void train_controller_init(void)
{
    esp_event_handler_register(TRAIN_EVENT, TRAIN_EVT_ENCODER_CW,        on_encoder_event, NULL);
    esp_event_handler_register(TRAIN_EVENT, TRAIN_EVT_ENCODER_CCW,       on_encoder_event, NULL);
    esp_event_handler_register(TRAIN_EVENT, TRAIN_EVT_ENCODER_CLICK,     on_encoder_event, NULL);
    esp_event_handler_register(TRAIN_EVENT, TRAIN_EVT_BUTTON_PRESS,      on_button_event,  NULL);
    esp_event_handler_register(TRAIN_EVENT, TRAIN_EVT_BUTTON_LONG_PRESS, on_button_event,  NULL);
    esp_event_handler_register(TRAIN_EVENT, TRAIN_EVT_MQTT_MESSAGE,      on_mqtt_message,  NULL);

    update_display();
    ESP_LOGI(TAG, "train controller initialized");
}
