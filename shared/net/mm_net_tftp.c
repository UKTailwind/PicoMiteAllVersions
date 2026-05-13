/*
 * shared/net/mm_net_tftp.c - small TFTP server protocol core.
 */

#include <stdio.h>
#include <string.h>

#include "shared/net/mm_net_tftp.h"

enum {
    TFTP_RRQ = 1,
    TFTP_WRQ = 2,
    TFTP_DATA = 3,
    TFTP_ACK = 4,
    TFTP_ERROR = 5,
    TFTP_OACK = 6,
    TFTP_DEFAULT_BLOCK_SIZE = 512,
    TFTP_MAX_BLOCK_SIZE = 512,
};

static uint16_t read_u16(const uint8_t *p) {
    return ((uint16_t)p[0] << 8) | p[1];
}

static void write_u16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static int same_peer(const mm_net_tftp_peer_t *a,
                     const mm_net_tftp_peer_t *b) {
    size_t len;
    if (a->family != b->family || a->port != b->port) return 0;
    len = a->family == 4 ? 4 : a->family == 6 ? 16 : 0;
    return len && memcmp(a->bytes, b->bytes, len) == 0;
}

static int parse_request_filename(const uint8_t *packet, size_t len,
                                  char *filename, size_t filename_len) {
    size_t pos = 2;
    size_t start = pos;
    if (len < 4 || filename_len == 0) return 0;
    while (pos < len && packet[pos]) pos++;
    if (pos == len || pos == start || pos - start >= filename_len) return 0;
    memcpy(filename, packet + start, pos - start);
    filename[pos - start] = 0;
    for (char *p = filename; *p; ++p) {
        if (*p == '\\') *p = '/';
    }
    if (filename[0] == '/' || strstr(filename, "..")) return 0;
    return 1;
}

static int ascii_tolower(int c) {
    if (c >= 'A' && c <= 'Z') return c + ('a' - 'A');
    return c;
}

static int ascii_strcasecmp(const char *a, const char *b) {
    while (*a && *b) {
        int ca = ascii_tolower((unsigned char)*a++);
        int cb = ascii_tolower((unsigned char)*b++);
        if (ca != cb) return ca - cb;
    }
    return ascii_tolower((unsigned char)*a) -
           ascii_tolower((unsigned char)*b);
}

static int parse_uint16_option(const char *s, uint16_t *out) {
    unsigned long v = 0;
    if (!s || !*s || !out) return 0;
    while (*s) {
        if (*s < '0' || *s > '9') return 0;
        v = v * 10u + (unsigned long)(*s - '0');
        if (v > 65535u) return 0;
        s++;
    }
    *out = (uint16_t)v;
    return 1;
}

static size_t next_zstring(const uint8_t *packet, size_t len, size_t pos,
                           const char **out) {
    size_t start = pos;
    if (out) *out = NULL;
    if (pos >= len) return len + 1;
    while (pos < len && packet[pos]) pos++;
    if (pos >= len) return len + 1;
    if (out) *out = (const char *)packet + start;
    return pos + 1;
}

static uint16_t parse_request_block_size(const uint8_t *packet, size_t len,
                                         int *accepted_option) {
    size_t pos = 2;
    const char *ignored;
    uint16_t block_size = TFTP_DEFAULT_BLOCK_SIZE;
    if (accepted_option) *accepted_option = 0;

    pos = next_zstring(packet, len, pos, &ignored);  /* filename */
    pos = next_zstring(packet, len, pos, &ignored);  /* mode */
    while (pos < len) {
        const char *key = NULL;
        const char *value = NULL;
        pos = next_zstring(packet, len, pos, &key);
        if (pos > len) break;
        pos = next_zstring(packet, len, pos, &value);
        if (pos > len) break;
        if (key && value && ascii_strcasecmp(key, "blksize") == 0) {
            uint16_t requested = 0;
            if (parse_uint16_option(value, &requested) && requested >= 8) {
                block_size = requested > TFTP_MAX_BLOCK_SIZE ?
                             TFTP_MAX_BLOCK_SIZE : requested;
                if (accepted_option) *accepted_option = 1;
            }
        }
    }
    return block_size;
}

static void send_ack(mm_net_tftp_session_t *session,
                     const mm_net_tftp_peer_t *peer, uint16_t block) {
    uint8_t packet[4];
    write_u16(packet, TFTP_ACK);
    write_u16(packet + 2, block);
    session->ops->send(session->ctx, peer, packet, sizeof(packet));
}

static void send_error(mm_net_tftp_session_t *session,
                       const mm_net_tftp_peer_t *peer,
                       uint16_t code, const char *message) {
    uint8_t packet[128];
    size_t msg_len = strlen(message);
    if (msg_len > sizeof(packet) - 5) msg_len = sizeof(packet) - 5;
    write_u16(packet, TFTP_ERROR);
    write_u16(packet + 2, code);
    memcpy(packet + 4, message, msg_len);
    packet[4 + msg_len] = 0;
    session->ops->send(session->ctx, peer, packet, msg_len + 5);
}

static void send_oack(mm_net_tftp_session_t *session,
                      const mm_net_tftp_peer_t *peer) {
    uint8_t packet[32];
    char value[8];
    size_t pos = 2;
    const char key[] = "blksize";
    write_u16(packet, TFTP_OACK);
    memcpy(packet + pos, key, sizeof(key));
    pos += sizeof(key);
    snprintf(value, sizeof(value), "%u", (unsigned)session->block_size);
    size_t value_len = strlen(value) + 1;
    memcpy(packet + pos, value, value_len);
    pos += value_len;
    session->ops->send(session->ctx, peer, packet, pos);
}

void mm_net_tftp_init(mm_net_tftp_session_t *session,
                      const mm_net_tftp_ops_t *ops, void *ctx) {
    memset(session, 0, sizeof(*session));
    session->ops = ops;
    session->ctx = ctx;
}

void mm_net_tftp_close(mm_net_tftp_session_t *session) {
    if (session->active && session->ops && session->ops->close) {
        session->ops->close(session->ctx, session->handle);
    }
    const mm_net_tftp_ops_t *ops = session->ops;
    void *ctx = session->ctx;
    memset(session, 0, sizeof(*session));
    session->ops = ops;
    session->ctx = ctx;
}

static void send_data_block(mm_net_tftp_session_t *session) {
    uint8_t packet[4 + TFTP_MAX_BLOCK_SIZE];
    ssize_t n;
    if (!session->active || session->write_mode) return;
    write_u16(packet, TFTP_DATA);
    write_u16(packet + 2, session->block);
    n = session->ops->read(session->ctx, session->handle,
                           packet + 4, session->block_size);
    if (n < 0) {
        send_error(session, &session->peer, 0, "read failed");
        mm_net_tftp_close(session);
        return;
    }
    session->last_data = n < session->block_size;
    session->ops->send(session->ctx, &session->peer, packet, (size_t)n + 4);
}

static void start_read(mm_net_tftp_session_t *session,
                       const mm_net_tftp_peer_t *peer,
                       const uint8_t *packet, size_t len) {
    char filename[256];
    void *handle = NULL;
    int accepted_option = 0;
    if (!parse_request_filename(packet, len, filename, sizeof(filename))) {
        send_error(session, peer, 4, "bad request");
        return;
    }
    uint16_t block_size = parse_request_block_size(packet, len,
                                                  &accepted_option);
    mm_net_tftp_close(session);
    if (session->ops->open(session->ctx, filename, 0, &handle) != 0) {
        send_error(session, peer, 1, "not found");
        return;
    }
    session->active = 1;
    session->write_mode = 0;
    session->last_data = 0;
    session->block = 1;
    session->block_size = block_size;
    session->peer = *peer;
    session->handle = handle;
    if (accepted_option) {
        send_oack(session, peer);
    } else {
        send_data_block(session);
    }
}

static void start_write(mm_net_tftp_session_t *session,
                        const mm_net_tftp_peer_t *peer,
                        const uint8_t *packet, size_t len) {
    char filename[256];
    void *handle = NULL;
    int accepted_option = 0;
    if (!parse_request_filename(packet, len, filename, sizeof(filename))) {
        send_error(session, peer, 4, "bad request");
        return;
    }
    uint16_t block_size = parse_request_block_size(packet, len,
                                                  &accepted_option);
    mm_net_tftp_close(session);
    if (session->ops->open(session->ctx, filename, 1, &handle) != 0) {
        send_error(session, peer, 2, "access denied");
        return;
    }
    session->active = 1;
    session->write_mode = 1;
    session->last_data = 0;
    session->block = 1;
    session->block_size = block_size;
    session->peer = *peer;
    session->handle = handle;
    if (accepted_option)
        send_oack(session, peer);
    else
        send_ack(session, peer, 0);
}

void mm_net_tftp_handle_packet(mm_net_tftp_session_t *session,
                               const mm_net_tftp_peer_t *peer,
                               const void *packet, size_t len) {
    const uint8_t *bytes = (const uint8_t *)packet;
    uint16_t opcode;
    if (!session || !session->ops || !peer || !packet || len < 2) return;
    opcode = read_u16(bytes);

    if (opcode == TFTP_RRQ) {
        start_read(session, peer, bytes, len);
    } else if (opcode == TFTP_WRQ) {
        start_write(session, peer, bytes, len);
    } else if (opcode == TFTP_DATA) {
        if (!session->active || !session->write_mode ||
            !same_peer(&session->peer, peer) || len < 4)
            return;
        uint16_t block = read_u16(bytes + 2);
        if (block != session->block) {
            send_ack(session, peer, (uint16_t)(session->block - 1));
            return;
        }
        size_t payload_len = len - 4;
        if (payload_len &&
            session->ops->write(session->ctx, session->handle,
                                bytes + 4, payload_len) !=
                (ssize_t)payload_len) {
            send_error(session, peer, 0, "write failed");
            mm_net_tftp_close(session);
            return;
        }
        send_ack(session, peer, block);
        session->block++;
        if (payload_len < session->block_size) mm_net_tftp_close(session);
    } else if (opcode == TFTP_ACK) {
        if (!session->active || session->write_mode ||
            !same_peer(&session->peer, peer) || len < 4)
            return;
        uint16_t block = read_u16(bytes + 2);
        if (block == 0 && session->block == 1) {
            send_data_block(session);
            return;
        }
        if (block != session->block) return;
        if (session->last_data) {
            mm_net_tftp_close(session);
        } else {
            session->block++;
            send_data_block(session);
        }
    } else if (opcode == TFTP_ERROR) {
        if (session->active && same_peer(&session->peer, peer))
            mm_net_tftp_close(session);
    }
}
