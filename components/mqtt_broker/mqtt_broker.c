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
    uint16_t keepalive_s;       /* Keepalive interval from CONNECT (seconds) */
    int64_t  last_activity_ms;  /* Timestamp of last received data */
    /* Last Will and Testament */
    char    *will_topic;
    char    *will_message;
    size_t   will_message_len;
    struct client *next;
} client_t;

static client_t *s_clients = NULL;
static subscription_t *s_subs = NULL;
static struct mg_mgr s_mgr;
static int s_client_count = 0;
static int s_sub_count = 0;

/* ─── Helpers ─── */

static void addr_to_str(struct mg_connection *c, char *buf, size_t len)
{
    mg_snprintf(buf, len, "%M", mg_print_ip_port, &c->rem);
}

/* Read a 2-byte length-prefixed string from buf at offset pos.
 * Returns number of bytes consumed (2 + string length), or 0 on error. */
static size_t read_mqtt_string(const uint8_t *buf, size_t total, size_t pos,
                               const char **out, size_t *out_len)
{
    if (pos + 2 > total) return 0;
    uint16_t slen = (uint16_t)(buf[pos] << 8) | buf[pos + 1];
    if (pos + 2 + slen > total) return 0;
    *out = (const char *)&buf[pos + 2];
    *out_len = slen;
    return 2 + slen;
}

static void add_client(struct mg_connection *c)
{
    client_t *cl = calloc(1, sizeof(*cl));
    if (cl) {
        cl->conn = c;
        cl->last_activity_ms = mg_millis();
        cl->next = s_clients;
        s_clients = cl;
        s_client_count++;
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
            s_sub_count--;
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
            free(tmp->will_topic);
            free(tmp->will_message);
            free(tmp);
            s_client_count--;
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
        s_sub_count++;
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

static client_t *find_client(struct mg_connection *c)
{
    for (client_t *cl = s_clients; cl; cl = cl->next) {
        if (cl->conn == c) return cl;
    }
    return NULL;
}

/**
 * Parse CONNECT packet variable header and payload to extract
 * keepalive interval and Last Will (topic + message).
 *
 * MQTT 3.1.1 CONNECT layout after fixed header:
 *   [2+N] Protocol Name ("MQTT")
 *   [1]   Protocol Level (4)
 *   [1]   Connect Flags
 *   [2]   Keep Alive (seconds)
 *   --- payload ---
 *   [2+N] Client Identifier
 *   [2+N] Will Topic     (if Will Flag set)
 *   [2+N] Will Message   (if Will Flag set)
 *   [2+N] Username       (if Username Flag set)
 *   [2+N] Password       (if Password Flag set)
 */
static void parse_connect_packet(client_t *cl, const struct mg_mqtt_message *mm)
{
    const uint8_t *p = (const uint8_t *)mm->dgram.buf;
    size_t total = mm->dgram.len;

    /* Skip fixed header: 1 byte cmd + variable length encoding */
    size_t hdr = 1;
    while (hdr < total && (p[hdr] & 0x80)) hdr++;
    hdr++;  /* skip last length byte */

    size_t pos = hdr;

    /* Protocol Name (skip) */
    const char *str; size_t slen;
    size_t consumed = read_mqtt_string(p, total, pos, &str, &slen);
    if (!consumed) return;
    pos += consumed;

    /* Protocol Level (1 byte) */
    if (pos >= total) return;
    pos++;

    /* Connect Flags (1 byte) */
    if (pos >= total) return;
    uint8_t flags = p[pos++];
    bool will_flag   = (flags >> 2) & 0x01;

    /* Keep Alive (2 bytes) */
    if (pos + 2 > total) return;
    cl->keepalive_s = (uint16_t)(p[pos] << 8) | p[pos + 1];
    pos += 2;

    /* Client Identifier (skip) */
    consumed = read_mqtt_string(p, total, pos, &str, &slen);
    if (!consumed) return;
    pos += consumed;

    /* Will Topic + Will Message */
    if (will_flag) {
        const char *wt; size_t wt_len;
        consumed = read_mqtt_string(p, total, pos, &wt, &wt_len);
        if (!consumed) return;
        pos += consumed;

        const char *wm; size_t wm_len;
        consumed = read_mqtt_string(p, total, pos, &wm, &wm_len);
        if (!consumed) return;
        pos += consumed;

        cl->will_topic = malloc(wt_len + 1);
        if (cl->will_topic) {
            memcpy(cl->will_topic, wt, wt_len);
            cl->will_topic[wt_len] = '\0';
        }
        cl->will_message = malloc(wm_len);
        if (cl->will_message) {
            memcpy(cl->will_message, wm, wm_len);
            cl->will_message_len = wm_len;
        }

        DLOG_I(TAG, "Client LWT: topic='%s' (%d bytes payload)",
               cl->will_topic ? cl->will_topic : "?", (int)wm_len);
    }

    DLOG_I(TAG, "Client keepalive: %d s", cl->keepalive_s);
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

        /* Update last activity timestamp for keepalive tracking */
        client_t *cl_activity = find_client(c);
        if (cl_activity) cl_activity->last_activity_ms = mg_millis();

        switch (mm->cmd) {
        case MQTT_CMD_CONNECT: {
            add_client(c);

            /* Parse keepalive & LWT from CONNECT packet */
            client_t *cl = find_client(c);
            if (cl) parse_connect_packet(cl, mm);

            char addr[48];
            addr_to_str(c, addr, sizeof(addr));
            DLOG_I(TAG, "Client CONNECT from %s (clients: %d)", addr, s_client_count);

            /* Send CONNACK */
            uint8_t ack[] = {0x20, 0x02, 0x00, 0x00};
            mg_send(c, ack, sizeof(ack));

            esp_event_post(TRAIN_EVENT, TRAIN_EVT_MQTT_CLIENT_CONNECT,
                           NULL, 0, 0);
            break;
        }

        case MQTT_CMD_SUBSCRIBE: {
            /*
             * mm->topic is only populated for PUBLISH in Mongoose.
             * For SUBSCRIBE, parse topic filters from the raw packet (mm->dgram).
             *
             * SUBSCRIBE packet layout (after fixed header):
             *   [2 bytes] Packet ID
             *   For each subscription:
             *     [2 bytes] Topic filter length (MSB, LSB)
             *     [N bytes] Topic filter
             *     [1 byte]  Requested QoS
             */
            const uint8_t *p = (const uint8_t *)mm->dgram.buf;
            size_t total = mm->dgram.len;

            /* Skip fixed header: 1 byte cmd + variable length encoding */
            size_t hdr = 1;
            while (hdr < total && (p[hdr] & 0x80)) hdr++;
            hdr++;  /* skip last length byte */

            /* Skip packet ID (2 bytes) */
            size_t pos = hdr + 2;
            char addr[48];
            addr_to_str(c, addr, sizeof(addr));

            /* Parse all topic filters in the payload */
            uint8_t suback_payload[32];
            size_t sub_count = 0;

            while (pos + 2 < total && sub_count < sizeof(suback_payload)) {
                uint16_t tlen = (uint16_t)(p[pos] << 8) | p[pos + 1];
                pos += 2;
                if (pos + tlen + 1 > total) break;

                struct mg_str topic = mg_str_n((const char *)&p[pos], tlen);
                pos += tlen;
                uint8_t qos = p[pos++];

                add_subscription(c, topic);
                DLOG_I(TAG, "SUBSCRIBE: '%.*s' QoS=%d by %s (total subs: %d)",
                       (int)topic.len, topic.buf, qos, addr, s_sub_count);
                suback_payload[sub_count++] = 0x00;  /* Granted QoS 0 */
            }

            /* Send SUBACK */
            uint8_t suback_hdr[] = {0x90,
                                    (uint8_t)(2 + sub_count),
                                    (uint8_t)(mm->id >> 8),
                                    (uint8_t)(mm->id & 0xFF)};
            mg_send(c, suback_hdr, sizeof(suback_hdr));
            mg_send(c, suback_payload, sub_count);
            break;
        }

        case MQTT_CMD_PUBLISH: {
            /* Count matching subscribers */
            int match_count = 0;
            for (subscription_t *sub = s_subs; sub; sub = sub->next) {
                if (topic_matches(sub->topic, mm->topic)) match_count++;
            }
            DLOG_I(TAG, "PUBLISH: '%.*s' (%d bytes) -> %d subscriber(s)",
                   (int)mm->topic.len, mm->topic.buf,
                   (int)mm->data.len, match_count);

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

        case MQTT_CMD_UNSUBSCRIBE: {
            /*
             * UNSUBSCRIBE packet layout (same as SUBSCRIBE but without QoS byte):
             *   Fixed header + Packet ID
             *   For each topic:
             *     [2 bytes] Topic filter length
             *     [N bytes] Topic filter
             */
            const uint8_t *p = (const uint8_t *)mm->dgram.buf;
            size_t total = mm->dgram.len;

            size_t hdr = 1;
            while (hdr < total && (p[hdr] & 0x80)) hdr++;
            hdr++;

            size_t pos = hdr + 2;  /* skip packet ID */
            char addr[48];
            addr_to_str(c, addr, sizeof(addr));

            while (pos + 2 < total) {
                uint16_t tlen = (uint16_t)(p[pos] << 8) | p[pos + 1];
                pos += 2;
                if (pos + tlen > total) break;

                struct mg_str topic = mg_str_n((const char *)&p[pos], tlen);
                pos += tlen;

                /* Remove matching subscription for this connection */
                subscription_t **sp = &s_subs;
                while (*sp) {
                    if ((*sp)->conn == c &&
                        (*sp)->topic.len == topic.len &&
                        memcmp((*sp)->topic.buf, topic.buf, topic.len) == 0) {
                        subscription_t *tmp = *sp;
                        *sp = tmp->next;
                        free((void *)tmp->topic.buf);
                        free(tmp);
                        s_sub_count--;
                        break;
                    }
                    sp = &(*sp)->next;
                }

                DLOG_I(TAG, "UNSUBSCRIBE: '%.*s' by %s (total subs: %d)",
                       (int)topic.len, topic.buf, addr, s_sub_count);
            }

            /* Send UNSUBACK */
            uint8_t unsuback[] = {0xB0, 0x02,
                                  (uint8_t)(mm->id >> 8),
                                  (uint8_t)(mm->id & 0xFF)};
            mg_send(c, unsuback, sizeof(unsuback));
            break;
        }

        case MQTT_CMD_PINGREQ: {
            mg_mqtt_pong(c);
            break;
        }

        case MQTT_CMD_DISCONNECT: {
            char addr[48];
            addr_to_str(c, addr, sizeof(addr));

            /* Per MQTT spec: graceful disconnect — discard LWT */
            client_t *cl_disc = find_client(c);
            if (cl_disc) {
                free(cl_disc->will_topic);
                cl_disc->will_topic = NULL;
                free(cl_disc->will_message);
                cl_disc->will_message = NULL;
            }

            remove_client(c);
            DLOG_I(TAG, "Client DISCONNECT from %s (clients: %d, subs: %d)",
                   addr, s_client_count, s_sub_count);
            esp_event_post(TRAIN_EVENT, TRAIN_EVT_MQTT_CLIENT_DISCONNECT,
                           NULL, 0, 0);
            c->is_draining = 1;
            break;
        }

        default:
            break;
        }

    } else if (ev == MG_EV_CLOSE) {
        /* Check if this was a known client (unexpected disconnect) */
        client_t *cl = find_client(c);
        if (cl) {
            char addr[48];
            addr_to_str(c, addr, sizeof(addr));

            /* Publish Last Will and Testament if set */
            if (cl->will_topic && cl->will_message) {
                DLOG_W(TAG, "Publishing LWT for %s on '%s'",
                       addr, cl->will_topic);
                struct mg_str t = mg_str(cl->will_topic);
                struct mg_str p = mg_str_n(cl->will_message, cl->will_message_len);
                publish_to_subscribers(t, p);

                /* Post LWT as internal event too */
                if (strlen(cl->will_topic) < 128 && cl->will_message_len < 256) {
                    mqtt_message_event_t evt = {0};
                    memcpy(evt.topic, cl->will_topic, strlen(cl->will_topic));
                    memcpy(evt.payload, cl->will_message, cl->will_message_len);
                    evt.payload_len = cl->will_message_len;
                    esp_event_post(TRAIN_EVENT, TRAIN_EVT_MQTT_MESSAGE,
                                   &evt, sizeof(evt), 0);
                }
            }

            remove_client(c);
            DLOG_W(TAG, "Client lost connection from %s (clients: %d, subs: %d)",
                   addr, s_client_count, s_sub_count);
            esp_event_post(TRAIN_EVENT, TRAIN_EVT_MQTT_CLIENT_DISCONNECT,
                           NULL, 0, 0);
        }
    }
}

/* ─── FreeRTOS task ─── */

/**
 * Check all connected clients for keepalive timeout.
 * Per MQTT spec, broker should disconnect if no message received
 * within 1.5x the keepalive interval.
 */
static void check_keepalive_timeouts(void)
{
    int64_t now = mg_millis();
    client_t *cl = s_clients;
    while (cl) {
        client_t *next = cl->next;  /* save next — close may remove cl */
        if (cl->keepalive_s > 0) {
            int64_t timeout_ms = (int64_t)cl->keepalive_s * 1500;  /* 1.5x */
            if (now - cl->last_activity_ms > timeout_ms) {
                char addr[48];
                addr_to_str(cl->conn, addr, sizeof(addr));
                DLOG_W(TAG, "Keepalive timeout for %s (no data for %lld ms)",
                       addr, (long long)(now - cl->last_activity_ms));
                cl->conn->is_draining = 1;  /* trigger MG_EV_CLOSE */
            }
        }
        cl = next;
    }
}

static void mqtt_broker_task(void *arg)
{
    char listen_url[32];
    snprintf(listen_url, sizeof(listen_url), "mqtt://0.0.0.0:%d", CONFIG_MQTT_BROKER_PORT);

    mg_mgr_init(&s_mgr);
    mg_mqtt_listen(&s_mgr, listen_url, mqtt_ev_handler, NULL);

    DLOG_I(TAG, "MQTT broker listening on port %d", CONFIG_MQTT_BROKER_PORT);

    for (;;) {
        mg_mgr_poll(&s_mgr, 100);
        check_keepalive_timeouts();
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
