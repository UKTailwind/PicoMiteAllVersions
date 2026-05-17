/***********************************************************************************************************************
PicoMite MMBasic

MMTCPclient.c

<COPYRIGHT HOLDERS>  Geoff Graham, Peter Mather
Copyright (c) 2021, <COPYRIGHT HOLDERS> All rights reserved.
Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
1.	Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
2.	Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer
    in the documentation and/or other materials provided with the distribution.
3.	The name MMBasic be used when referring to the interpreter in any documentation and promotional material and the original copyright message be displayed
    on the console at startup (additional copyright messages may be added).
4.	All advertising materials mentioning features or use of this software must display the following acknowledgement: This product includes software developed
    by the <copyright holder>.
5.	Neither the name of the <copyright holder> nor the names of its contributors may be used to endorse or promote products derived from this software
    without specific prior written permission.
THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDERS> AS IS AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDERS> BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

************************************************************************************************************************/
#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "lwip/altcp.h"
#include "lwip/altcp_tcp.h"
#ifdef PICOMITEWEB_TLS
#include "lwip/altcp_tls.h"
#include "mbedtls/ssl.h"
#endif
#define DEBUG_printf
TCP_CLIENT_T *TCP_CLIENT = NULL;
int streampointer = 0;
// Perform initialisation
TCP_CLIENT_T *tcp_client_init(void)
{
    TCP_CLIENT_T *state = calloc(1, sizeof(TCP_CLIENT_T));
    if (!state)
    {
        //        DEBUG_printf("failed to allocate state\n");
        return NULL;
    }
    //  ip4addr_aton(TEST_TCP_SERVER_IP, &state->remote_addr);
    return state;
}
// Call back with a DNS result
void tcp_dns_found(const char *hostname, const ip_addr_t *ipaddr, void *arg)
{
    TCP_CLIENT_T *state = (TCP_CLIENT_T *)arg;
    if (ipaddr)
    {
        state->remote_addr = *ipaddr;
        char buff[STRINGSIZE] = {0};
        sprintf(buff, "tcp address %s\r\n", ip4addr_ntoa(ipaddr));
        if (!optionsuppressstatus)
            MMPrintString(buff);
        state->complete = 1;
        //        ntp_request(state);
    }
    else
    {
        if (TCP_CLIENT == state)
            TCP_CLIENT = NULL;
        free(state);
        web_async_set_error("tcp dns request failed");
    }
}
static err_t tcp_client_close(void *arg)
{
    TCP_CLIENT_T *state = (TCP_CLIENT_T *)arg;
    err_t err = ERR_OK;
    if (state->tcp_pcb != NULL)
    {
        struct altcp_pcb *pcb = (struct altcp_pcb *)state->tcp_pcb;
        altcp_arg(pcb, NULL);
        altcp_poll(pcb, NULL, 0);
        altcp_sent(pcb, NULL);
        altcp_recv(pcb, NULL);
        altcp_err(pcb, NULL);
        err = altcp_close(pcb);
        if (err != ERR_OK)
        {
            altcp_abort(pcb);
            err = ERR_ABRT;
        }
        state->tcp_pcb = NULL;
    }
    return err;
}
static err_t tcp_client_sent(void *arg, struct altcp_pcb *tpcb, u16_t len)
{
    return ERR_OK;
}

static void tcp_client_err(void *arg, err_t err)
{
    if (err != ERR_ABRT)
    {
        /* lwIP has already aborted the pcb. Drop our reference so the next
           command starting up doesn't try to reuse a dead pcb. */
        TCP_CLIENT_T *state = (TCP_CLIENT_T *)arg;
        if (state)
            state->tcp_pcb = NULL;
        /* Include the err_t code for diagnosis. For TLS connections this is
           usually ERR_ABRT (above, silently ignored) or ERR_CLSD/ERR_RST
           when the server reset us mid-handshake; in either case a
           verification failure on the upper TLS layer is the most common
           root cause. tls_state in MMTCPclient.c marks state->tls so the
           message hints at TLS. */
        char buf[64];
        snprintf(buf, sizeof(buf), "%s client error %d",
                 (state && state->tls) ? "TLS" : "TCP", (int)err);
        web_async_set_error(buf);
    }
}
err_t tcp_client_recv(void *arg, struct altcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    TCP_CLIENT_T *state = (TCP_CLIENT_T *)arg;
    if (!p)
    {
        /* Remote sent FIN — mark the state dead but do NOT close from inside
           the recv callback. Calling altcp_close (and especially altcp_recv
           with NULL) while we're still inside the callback for this pcb has
           been observed to break subsequent connections that share the same
           altcp_tls config — the next handshake completes but data never
           flows back to recv. The actual close happens cleanly when the
           user's WEB CLOSE TCP CLIENT runs (or the implicit close at the
           next WEB OPEN TCP/TLS CLIENT). The pcb sits in CLOSE_WAIT until
           then, which is bounded because TCP_CLIENT only ever holds one. */
        state->connected = false;
        return ERR_OK;
    }
    if (p->tot_len > 0)
    {
        routinechecks(); // don't know why I'm doing this but it solves a race condition for the RP2350
        const uint16_t buffer_left = state->BUF_SIZE - state->buffer_len;
        state->buffer_len += pbuf_copy_partial(p, (void *)state->buffer + state->buffer_len,
                                               p->tot_len > buffer_left ? buffer_left : p->tot_len, 0);
        altcp_recved(tpcb, p->tot_len);
        cyw43_arch_lwip_begin();
        uint64_t *x = (uint64_t *)state->buffer;
        x--;
        *x = state->buffer_len;
        cyw43_arch_lwip_end();
    }
    pbuf_free(p);
    return ERR_OK;
}
err_t tcp_client_recv_stream(void *arg, struct altcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    TCP_CLIENT_T *state = (TCP_CLIENT_T *)arg;
    if (!p)
    {
        /* See note in tcp_client_recv — defer close to the user/main thread
           to avoid corrupting the shared altcp_tls config state. */
        state->connected = false;
        return ERR_OK;
    }
    if (p->tot_len > 0)
    {
        /* For chained pbufs we need to walk the chain — payload is only the
           first segment. Use pbuf_get_at to avoid that complication. */
        for (int j = 0; j < p->tot_len; j++)
        {
            state->buffer[*state->buffer_write] = pbuf_get_at(p, j);
            *state->buffer_write = (*state->buffer_write + 1) % state->BUF_SIZE; // advance head
            if (*state->buffer_write == *state->buffer_read)
            {
                *state->buffer_read = (*state->buffer_read + 1) % state->BUF_SIZE; // discard oldest
            }
        }
        altcp_recved(tpcb, p->tot_len);
    }
    pbuf_free(p);
    return ERR_OK;
}
static err_t tcp_client_connected(void *arg, struct altcp_pcb *tpcb, err_t err)
{
    TCP_CLIENT_T *state = (TCP_CLIENT_T *)arg;
    if (err != ERR_OK)
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "connect failed %d", (int)err);
        web_async_set_error(buf);
        return err;
    }
    if (!optionsuppressstatus)
        MMPrintString("Connected\r\n");
    state->connected = true;
    return ERR_OK;
}

/* hostname_for_sni is non-NULL only for the TLS path AND only when the user
   typed a name (not a dotted-quad). Passes through to mbedtls_ssl_set_hostname
   so the server can return the correct virtual-host certificate. */
static bool tcp_client_open(void *arg, const char *hostname_for_sni)
{
    TCP_CLIENT_T *state = (TCP_CLIENT_T *)arg;
    char buff[STRINGSIZE] = {0};
    sprintf(buff, "Connecting to %s port %u%s\r\n",
            ip4addr_ntoa(&state->remote_addr), state->TCP_PORT,
            state->tls ? " (TLS)" : "");
    if (!optionsuppressstatus)
        MMPrintString(buff);

    struct altcp_pcb *pcb = NULL;
    if (state->tls)
    {
#ifdef PICOMITEWEB_TLS
        struct altcp_tls_config *cfg = picomite_tls_get_client_config();
        if (!cfg)
        {
            error("failed to create TLS config");
            return false;
        }
        pcb = altcp_tls_new(cfg, IP_GET_TYPE(&state->remote_addr));
        if (pcb && hostname_for_sni)
        {
            mbedtls_ssl_context *ssl = altcp_tls_context(pcb);
            if (ssl)
                mbedtls_ssl_set_hostname(ssl, hostname_for_sni);
        }
#else
        error("TLS not built in");
        return false;
#endif
    }
    else
    {
        pcb = altcp_tcp_new_ip_type(IP_GET_TYPE(&state->remote_addr));
    }
    state->tcp_pcb = pcb;
    if (!pcb)
    {
        error("failed to create pcb");
        return false;
    }

    altcp_arg(pcb, state);
    altcp_sent(pcb, tcp_client_sent);
    if (state->buffer_write == NULL)
        altcp_recv(pcb, tcp_client_recv);
    else
        altcp_recv(pcb, tcp_client_recv_stream);
    altcp_err(pcb, tcp_client_err);

    state->buffer_len = 0;

    err_t err = altcp_connect(pcb, &state->remote_addr, state->TCP_PORT, tcp_client_connected);
    return err == ERR_OK;
}
void close_tcpclient(void)
{
    TCP_CLIENT_T *state = TCP_CLIENT;
    if (!state)
        return;
    tcp_client_close(state);
    free(state);
    TCP_CLIENT = NULL;
}

int cmd_tcpclient(void)
{
    unsigned char *tp;
    tp = checkstring(cmdline, (unsigned char *)"OPEN TCP CLIENT");
    if (tp)
    {
        int timeout = 5000;
        getcsargs(&tp, 5);
        if (argc < 3)
            SyntaxError();
        ;
        ip4_addr_t remote_addr;
        char *IP = GetTempStrMemory();
        if (TCP_CLIENT)
            close_tcpclient();
        TCP_CLIENT_T *state = tcp_client_init();
        IP = (char *)getCstring(argv[0]);
        int port = getint(argv[2], 1, 65535);
        if (argc == 5)
            timeout = getint(argv[4], 1, 100000);
        TCP_CLIENT = state;
        state->TCP_PORT = port;
        state->buffer_write = NULL;
        int dots = 0;
        for (const char *p = IP; *p; p++) if (*p == '.') dots++;
        if (dots == 3 && ip4addr_aton(IP, &remote_addr))
        {
            state->remote_addr = remote_addr;
        }
        else
        {
            int err = dns_gethostbyname(IP, &remote_addr, tcp_dns_found, state);
            if (err == ERR_OK)
                state->remote_addr = remote_addr;
            else if (err == ERR_INPROGRESS)
            {
                Timer4 = timeout;
                while (!state->complete && Timer4 && !(err == ERR_OK))
                    if (startupcomplete)
                        cyw43_arch_poll();
                web_async_check_error();
                if (!Timer4)
                    error("Failed to convert web address");
                state->complete = 0;
            }
            else
                error("Failed to find TCP address");
        }
        if (!tcp_client_open(state, NULL))
        {
            error("Failed to open client");
        }

        Timer4 = timeout;
        while (!state->connected && Timer4)
            if (startupcomplete)
                cyw43_arch_poll();
        web_async_check_error();
        if (!Timer4)
            error("No response from client");
        return 1;
    }
    tp = checkstring(cmdline, (unsigned char *)"OPEN TCP STREAM");
    if (tp)
    {
        int timeout = 5000;
        getcsargs(&tp, 5);
        if (argc < 3)
            SyntaxError();
        ;
        ip4_addr_t remote_addr;
        char *IP = GetTempStrMemory();
        if (TCP_CLIENT)
            close_tcpclient();
        TCP_CLIENT_T *state = tcp_client_init();
        IP = (char *)getCstring(argv[0]);
        int port = getint(argv[2], 1, 65535);
        if (argc == 5)
            timeout = getint(argv[4], 1, 100000);
        TCP_CLIENT = state;
        state->TCP_PORT = port;
        state->buffer_write = &streampointer;
        int dots = 0;
        for (const char *p = IP; *p; p++) if (*p == '.') dots++;
        if (dots == 3 && ip4addr_aton(IP, &remote_addr))
        {
            state->remote_addr = remote_addr;
        }
        else
        {
            int err = dns_gethostbyname(IP, &remote_addr, tcp_dns_found, state);
            if (err == ERR_OK)
                state->remote_addr = remote_addr;
            else if (err == ERR_INPROGRESS)
            {
                Timer4 = timeout;
                while (!state->complete && Timer4 && !(err == ERR_OK))
                    ProcessWeb(0);
                if (!Timer4)
                    error("Failed to convert web address");
                state->complete = 0;
            }
            else
                error("Failed to find TCP address");
        }
        if (!tcp_client_open(state, NULL))
        {
            error("Failed to open client");
        }

        Timer4 = timeout;
        while (!state->connected && Timer4)
        {
            {
                if (startupcomplete)
                    ProcessWeb(0);
            }
        }
        if (!Timer4)
            error("No response from client");
        return 1;
    }

#ifdef PICOMITEWEB_TLS
    /* OPEN TLS CLIENT / OPEN TLS STREAM — same arguments as the plain TCP
       variants. The hostname (when not a dotted-quad IP literal) is also used
       as the SNI hostname so virtual-host HTTPS servers return the right
       certificate. Peer-cert verification is currently DISABLED (no CA bundle
       baked in). */
    tp = checkstring(cmdline, (unsigned char *)"OPEN TLS CLIENT");
    if (tp)
    {
        int timeout = 5000;
        getcsargs(&tp, 5);
        if (argc < 3)
            SyntaxError();
        ;
        ip4_addr_t remote_addr;
        char *IP = GetTempStrMemory();
        if (TCP_CLIENT)
            close_tcpclient();
        TCP_CLIENT_T *state = tcp_client_init();
        IP = (char *)getCstring(argv[0]);
        int port = getint(argv[2], 1, 65535);
        if (argc == 5)
            timeout = getint(argv[4], 1, 100000);
        TCP_CLIENT = state;
        state->TCP_PORT = port;
        state->buffer_write = NULL;
        state->tls = true;
        int dots = 0;
        for (const char *p = IP; *p; p++) if (*p == '.') dots++;
        bool is_ip = (dots == 3 && ip4addr_aton(IP, &remote_addr));
        if (is_ip)
        {
            state->remote_addr = remote_addr;
        }
        else
        {
            int err = dns_gethostbyname(IP, &remote_addr, tcp_dns_found, state);
            if (err == ERR_OK)
                state->remote_addr = remote_addr;
            else if (err == ERR_INPROGRESS)
            {
                Timer4 = timeout;
                while (!state->complete && Timer4 && !(err == ERR_OK))
                    if (startupcomplete)
                        cyw43_arch_poll();
                web_async_check_error();
                if (!Timer4)
                    error("Failed to convert web address");
                state->complete = 0;
            }
            else
                error("Failed to find TLS address");
        }
        if (!tcp_client_open(state, is_ip ? NULL : IP))
        {
            error("Failed to open TLS client");
        }

        Timer4 = timeout;
        while (!state->connected && Timer4)
            if (startupcomplete)
                cyw43_arch_poll();
        web_async_check_error();
        if (!Timer4)
            error("No response from TLS server (handshake timeout)");
        return 1;
    }
    tp = checkstring(cmdline, (unsigned char *)"OPEN TLS STREAM");
    if (tp)
    {
        int timeout = 5000;
        getcsargs(&tp, 5);
        if (argc < 3)
            SyntaxError();
        ;
        ip4_addr_t remote_addr;
        char *IP = GetTempStrMemory();
        if (TCP_CLIENT)
            close_tcpclient();
        TCP_CLIENT_T *state = tcp_client_init();
        IP = (char *)getCstring(argv[0]);
        int port = getint(argv[2], 1, 65535);
        if (argc == 5)
            timeout = getint(argv[4], 1, 100000);
        TCP_CLIENT = state;
        state->TCP_PORT = port;
        state->buffer_write = &streampointer;
        state->tls = true;
        int dots = 0;
        for (const char *p = IP; *p; p++) if (*p == '.') dots++;
        bool is_ip = (dots == 3 && ip4addr_aton(IP, &remote_addr));
        if (is_ip)
        {
            state->remote_addr = remote_addr;
        }
        else
        {
            int err = dns_gethostbyname(IP, &remote_addr, tcp_dns_found, state);
            if (err == ERR_OK)
                state->remote_addr = remote_addr;
            else if (err == ERR_INPROGRESS)
            {
                Timer4 = timeout;
                while (!state->complete && Timer4 && !(err == ERR_OK))
                    ProcessWeb(0);
                if (!Timer4)
                    error("Failed to convert web address");
                state->complete = 0;
            }
            else
                error("Failed to find TLS address");
        }
        if (!tcp_client_open(state, is_ip ? NULL : IP))
        {
            error("Failed to open TLS client");
        }

        Timer4 = timeout;
        while (!state->connected && Timer4)
        {
            if (startupcomplete)
                ProcessWeb(0);
        }
        if (!Timer4)
            error("No response from TLS server (handshake timeout)");
        return 1;
    }
#endif /* PICOMITEWEB_TLS */

    tp = checkstring(cmdline, (unsigned char *)"TCP CLIENT REQUEST");
    if (tp)
    {
        int64_t *dest = NULL;
        uint8_t *q = NULL;
        int size = 0, timeout = 5000;
        TCP_CLIENT_T *state = TCP_CLIENT;
        getcsargs(&tp, 5);
        if (!state)
            error("No connection");
        if (!state->connected)
            error("No connection");
        if (argc < 3)
            SyntaxError();
        ;
        char *request = (char *)getstring(argv[0]);
        size = parseintegerarray(argv[2], &dest, 2, 1, NULL, true, NULL) * 8;
        dest[0] = 0;
        q = (uint8_t *)&dest[1];
        if (argc == 5)
            timeout = getint(argv[4], 1, 100000);
        state->BUF_SIZE = size;
        state->buffer = q;
        state->buffer_len = 0;
        err_t err = altcp_write((struct altcp_pcb *)state->tcp_pcb, &request[1], (uint32_t)request[0], 0);
        if (err)
            error("write failed %", err);
        Timer4 = timeout;
        while (!state->buffer_len && Timer4)
            ProcessWeb(0);
        if (!Timer4)
            error("No response from server");
        else
            Timer4 = 200;
        while (Timer4)
            ProcessWeb(0);
        return 1;
    }
    tp = checkstring(cmdline, (unsigned char *)"TCP CLIENT STREAM");
    if (tp)
    {
        void *ptr1 = NULL;
        int64_t *dest = NULL;
        uint8_t *q = NULL;
        int size = 0;
        TCP_CLIENT_T *state = TCP_CLIENT;
        getcsargs(&tp, 7);
        if (!state)
            error("No connection");
        if (!state->connected)
            error("No connection");
        if (argc != 7)
            SyntaxError();
        ;
        char *request = (char *)getstring(argv[0]);
        size = parseintegerarray(argv[2], &dest, 2, 1, NULL, true, NULL) * 8;
        dest[0] = 0;
        q = (uint8_t *)&dest[1];
        ptr1 = findvar(argv[4], V_FIND | V_NOFIND_ERR);
        if (g_vartbl[g_VarIndex].type & T_INT)
        {
            if (g_vartbl[g_VarIndex].dims[0] != 0)
                error("Argument 3 must be an integer");
            state->buffer_read = (int *)ptr1;
        }
        else
            error("Argument 3 must be an integer");
        ptr1 = findvar(argv[6], V_FIND | V_NOFIND_ERR);
        if (g_vartbl[g_VarIndex].type & T_INT)
        {
            if (g_vartbl[g_VarIndex].dims[0] != 0)
                error("Argument 4 must be an integer");
            state->buffer_write = (int *)ptr1;
        }
        else
            error("Argument 4 must be an integer");
        state->BUF_SIZE = size;
        state->buffer = q;
        state->buffer_len = 0;
        err_t err = altcp_write((struct altcp_pcb *)state->tcp_pcb, &request[1], (uint32_t)request[0], 0);
        if (err)
            error("write failed %", err);
        return 1;
    }
    tp = checkstring(cmdline, (unsigned char *)"CLOSE TCP CLIENT");
    if (tp)
    {
        TCP_CLIENT_T *state = TCP_CLIENT;
        if (!state)
            error("No connection");
        close_tcpclient();
        return 1;
    }
    return 0;
}

#ifdef PICOMITEWEB_TLS
/* WEB TLS sub-commands.
   WEB TLS CA "filename"   — Load a CA bundle (PEM or DER) from the filesystem
                             and switch the TLS client config to
                             MBEDTLS_SSL_VERIFY_REQUIRED. Run WEB NTP first
                             so cert expiry verification has a real clock.
   WEB TLS NOVERIFY        — Drop any loaded CA, revert to no-verification
                             default (encrypted but not authenticated). */
int cmd_tls(void)
{
    unsigned char *tp;
    tp = checkstring(cmdline, (unsigned char *)"TLS CA");
    if (tp)
    {
        getcsargs(&tp, 1);
        if (argc != 1)
            SyntaxError();
        char *fname = (char *)getCstring(argv[0]);
        if (*fname == 0)
            error("Filename required");
        if (!ExistsFile(fname))
            error("Cannot find file");
        int size = FileSize(fname);
        if (size <= 0)
            error("Empty CA file");
        /* Allocate size + 1 so PEM is null-terminated for mbedtls_x509_crt_parse.
           Pass size+1 as ca_len so PEM detection works. DER will fail to
           parse with the trailing 0 but PEM is the common case for CA bundles. */
        int fn = FindFreeFileNbr();
        if (!BasicFileOpen(fname, fn, FA_READ))
            error("Cannot open CA file");
        unsigned char *buf = (unsigned char *)GetTempMainMemory(size + 1);
        UINT n_read = 0;
        FileGetData(fn, (char *)buf, size, &n_read);
        FileClose(fn);
        if ((int)n_read != size)
            error("Short read on CA file");
        buf[size] = 0;
        if (picomite_tls_set_ca(buf, (size_t)(size + 1)) != 0)
            error("Failed to parse CA bundle");
        if (!optionsuppressstatus)
            MMPrintString("TLS: CA bundle loaded, peer verification REQUIRED\r\n");
        return 1;
    }
    tp = checkstring(cmdline, (unsigned char *)"TLS NOVERIFY");
    if (tp)
    {
        picomite_tls_clear_ca();
        if (!optionsuppressstatus)
            MMPrintString("TLS: peer verification DISABLED\r\n");
        return 1;
    }
    return 0;
}
#endif
