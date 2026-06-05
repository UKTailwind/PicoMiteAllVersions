/*
 * shared/net/mm_net_web_cmd.c - shared top-level WEB command dispatch.
 */

#include "MMBasic_Includes.h"
#include "shared/net/mm_net_web_cmd.h"

void mm_net_web_dispatch(unsigned char * line,
                         const mm_net_web_dispatch_t * dispatch) {
    unsigned char * tp;

    if (!dispatch) error("Syntax");

    if ((tp = checkstring(line, (unsigned char *)"CONNECT"))) {
        if (!dispatch->connect) error("Syntax");
        dispatch->connect(tp);
        return;
    }

    if ((tp = checkstring(line, (unsigned char *)"SCAN"))) {
        if (!dispatch->scan) error("Syntax");
        dispatch->scan(tp);
        return;
    }

    if (dispatch->is_connected && !dispatch->is_connected()) {
        error((char *)(dispatch->not_connected_error ? dispatch->not_connected_error
                                                     : "WIFI not connected"));
    }

    if (dispatch->mqtt && dispatch->mqtt(line)) return;
    if (dispatch->tcp_client && dispatch->tcp_client(line)) return;

    if ((tp = checkstring(line, (unsigned char *)"TRANSMIT"))) {
        if (dispatch->transmit && dispatch->transmit(tp)) return;
    }

    if ((tp = checkstring(line, (unsigned char *)"TCP"))) {
        if (dispatch->tcp_server && dispatch->tcp_server(tp)) return;
    }

    if ((tp = checkstring(line, (unsigned char *)"NTP"))) {
        if (!dispatch->ntp) error("Syntax");
        dispatch->ntp(tp);
        return;
    }

    if ((tp = checkstring(line, (unsigned char *)"UDP"))) {
        if (dispatch->udp && dispatch->udp(tp)) return;
    }

    error("Syntax");
}
