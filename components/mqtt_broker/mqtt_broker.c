#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "mqtt_broker.h"
#include "mongoose.h"
#include "debug_log.h"
#include "event_bus.h"

static const char *TAG = "mqtt_broker";

/* ─── Subscription table ─── */

typedef struct subscription {
    struct mg_connection *conn;
    struct mg_str topic;
    struct subscription *next;
} subscription_t;

typedef struct client {
    struct mg_connection *conn;
    struct client *next;
} client_t;

static client_t *s_clients = NULL;
static subscription_t *s_subs = NULL;
static struct mg_mgr s_mgr;

/* ─── Helpers ─── */

static void add_client(struct mg_connection *c)
{
    client_t *cl = calloc(1, sizeof(*cl));
    if (cl) {
        cl->conn = c;
        cl->next = s_clients;
        s_clients = cl;
    }
}

static void remove_client(struct mg_connection *c)
{
    /* Remove all subscriptions for this client */
    subscription_t **sp = &s_subs;
    while (*sp) {
        if ((*sp)->conn == c) {
            subscription_t *tmp = *sp;
            *sp = tmp->next;
            free((void *)tmp->topic.buf);
            free(tmp);
        } else {
            sp = &(*sp)->next;
        }
    }

    /* Remove client from list */
    client_t **cp = &s_clients;
    while (*cp) {
        if ((*cp)->conn == c) {
            client_t *tmp = *cp;
            *cp = tmp->next;
            free(tmp);
            return;
        }
        cp = &(*cp)->next;
    }
}

static void add_subscription(struct mg_connection *c, struct mg_str topic)
{
    subscription_t *sub = calloc(1, sizeof(*sub));
    if (sub) {
        sub->conn = c;
        /* Deep-copy topic string */
        char *buf = malloc(topic.len + 1);
        if (buf) {
            memcpy(buf, topic.buf, topic.len);
            buf[topic.len] = '\0';
            sub->topic = mg_str(buf);
        }
        sub->next = s_subs;
        s_subs = sub;
    }
}

/**
 * Simple MQTT topic matching with wildcard support.
 * Supports '+' (single level) and '#' (multi level) wildcards.
 */
static bool topic_matches(struct mg_str filter, struct mg_str topic)
{
    const char *f = filter.buf, *fe = f + filter.len;
    const char *t = topic.buf, *te = t + topic.len;

    while (f < fe && t < te) {
        if (*f == '#') return true;
        if (*f == '+') {
            /* Skip one topic level in the topic */
            while (t < te && *t != '/') t++;
            f++;
            if (f < fe && *f == '/') f++;
            if (t < te && *t == '/') t++;
        } else {
            if (*f != *t) return false;
            f++;
            t++;
        }
    }

    /* '#' at end matches empty remainder */
    if (f < fe && *f == '#') return true;

    return (f == fe && t == te);
}

static void publish_to_subscribers(struct mg_str topic, struct mg_str payload)
{
    for (subscription_t *sub = s_subs; sub; sub = sub->next) {
        if (topic_matches(sub->topic, topic)) {
            struct mg_mqtt_opts pub_opts = {
                .topic = topic,
                .message = payload,
                .qos = 0,
            };
            mg_mqtt_pub(sub->conn, &pub_opts);
        }
    }
}

/* ─── Mongoose MQTT event handler ─── */

static void mqtt_ev_handler(struct mg_connection *c, int ev, void *ev_data)
{
    if (ev == MG_EV_MQTT_CMD) {
        struct mg_mqtt_message *mm = (struct mg_mqtt_message *)ev_data;

        switch (mm->cmd) {
        case MQTT_CMD_CONNECT: {
            {
                char addr[40];
                mg_snprintf(addr, sizeof(addr), "%M", mg_print_ip, &c->rem);
                DLOG_I(TAG, "Client connected from %s", addr);
            }
            add_client(c);

            /* Send CONNACK */
            uint8_t ack[] = {0x20, 0x02, 0x00, 0x00};
            mg_send(c, ack, sizeof(ack));

            /* Post event */
            esp_event_post(TRAIN_EVENT, TRAIN_EVT_MQTT_CLIENT_CONNECT,
                           NULL, 0, 0);
            break;
        }

        case MQTT_CMD_SUBSCRIBE: {
            DLOG_I(TAG, "Subscribe: %.*s", (int)mm->topic.len, mm->topic.buf);
            add_subscription(c, mm->topic);

            /* Send SUBACK */
            uint8_t ack[] = {0x90, 0x03,
                             (uint8_t)(mm->id >> 8), (uint8_t)(mm->id & 0xFF),
                             0x00};
            mg_send(c, ack, sizeof(ack));
            break;
        }

        case MQTT_CMD_PUBLISH: {
            DLOG_D(TAG, "Publish: %.*s -> %.*s",
                   (int)mm->topic.len, mm->topic.buf,
                   (int)mm->data.len, mm->data.buf);

            /* Fan-out to subscribers */
            publish_to_subscribers(mm->topic, mm->data);

            /* Post event to internal event bus */
            if (mm->topic.len < 128 && mm->data.len < 256) {
                mqtt_message_event_t evt = {0};
                memcpy(evt.topic, mm->topic.buf, mm->topic.len);
                memcpy(evt.payload, mm->data.buf, mm->data.len);
                evt.payload_len = mm->data.len;
                esp_event_post(TRAIN_EVENT, TRAIN_EVT_MQTT_MESSAGE,
                               &evt, sizeof(evt), 0);
            }
            break;
        }

        case MQTT_CMD_PINGREQ: {
            mg_mqtt_pong(c);
            break;
        }

        case MQTT_CMD_DISCONNECT: {
            DLOG_I(TAG, "Client disconnected gracefully");
            remove_client(c);
            esp_event_post(TRAIN_EVENT, TRAIN_EVT_MQTT_CLIENT_DISCONNECT,
                           NULL, 0, 0);
            c->is_draining = 1;
            break;
        }

        default:
            break;
        }

    } else if (ev == MG_EV_CLOSE) {
        remove_client(c);
    }
}

/* ─── FreeRTOS task ─── */

static void mqtt_broker_task(void *arg)
{
    char listen_url[32];
    snprintf(listen_url, sizeof(listen_url), "mqtt://0.0.0.0:%d", CONFIG_MQTT_BROKER_PORT);

    mg_mgr_init(&s_mgr);
    mg_mqtt_listen(&s_mgr, listen_url, mqtt_ev_handler, NULL);

    DLOG_I(TAG, "MQTT broker listening on port %d", CONFIG_MQTT_BROKER_PORT);

    for (;;) {
        mg_mgr_poll(&s_mgr, 100);
    }
}

/* ─── Public API ─── */

void mqtt_broker_init(void)
{
    xTaskCreatePinnedToCore(
        mqtt_broker_task,
        "mqtt_broker",
        4096,
        NULL,
        5,
        NULL,
        1  /* Pin to core 1 */
    );

    DLOG_I(TAG, "MQTT broker task started");
}

void mqtt_broker_publish_internal(const char *topic, const void *payload, size_t len)
{
    struct mg_str t = mg_str(topic);
    struct mg_str p = mg_str_n((const char *)payload, len);
    publish_to_subscribers(t, p);
}
