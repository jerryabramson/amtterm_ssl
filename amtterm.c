/*
 *  amtterm -- Intel AMT serial-over-lan client, console version.
 *
 *  Copyright (C) 2007 Gerd Hoffmann <kraxel@redhat.com
 *
 *  SSL helper functions by:
 *    Copyright (C) 2014 Andreas Steinmetz <ast@domdv.de>
 *
 *  SSL Client Certificates and logging improovements:
 *    Copyright (C) 2019 Jerry Abramson <jerry@jabramson.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include <signal.h>
#include <sys/ioctl.h>

#include "redir.h"

#define APPNAME "amtterm"
#define BUFSIZE 512

/* ------------------------------------------------------------------ */

static int recv_tty(void *cb_data, unsigned char *buf, int len)
{
//    struct redir *r = cb_data;

    return write(STDOUT_FILENO, buf, len);
}

static void state_tty(void *cb_data, enum redir_state old, enum redir_state new)
{
    struct redir *r = cb_data;

    if (r->verbose)
        fprintf(stderr, APPNAME ": TTY State Change: %s -> %s (%s)\n",
                redir_state_name(old), redir_state_name(new),
                redir_state_desc(new));
    switch (new) {
        case REDIR_RUN_SOL:
            printf("SOL connection established, use ^] to exit\n");
            break;
        case REDIR_ERROR:
            fprintf(stderr, APPNAME ": ERROR: %s\n", r->err);
            break;
        default:
            break;
    }
}

static int redir_loop(struct redir *r)
{
    unsigned char buf[BUFSIZE+1];
    struct timeval tv;
    int rc, i;
    fd_set set;

    for(;;) {
        if (r->state == REDIR_CLOSED || r->state == REDIR_ERROR) {
            printf("\nExited.\n");
            break;
        }

        FD_ZERO(&set);
        if (r->state == REDIR_RUN_SOL)
            FD_SET(STDIN_FILENO,&set);
        FD_SET(r->sock,&set);
        tv.tv_sec  = HEARTBEAT_INTERVAL * 4 / 1000;
        tv.tv_usec = 0;
        switch (select(r->sock+1,&set,NULL,NULL,&tv)) {
            case -1:
                perror("select");
                return -1;
            case 0:
                fprintf(stderr,"select: timeout\n");
                return -1;
        }

        if (FD_ISSET(STDIN_FILENO,&set)) {
            /* stdin has data */
            rc = read(STDIN_FILENO,buf,BUFSIZE);
            switch (rc) {
                case -1:
                    perror("read(stdin)");
                    return -1;
                case 0:
                    fprintf(stderr,"EOF from stdin\n");
                    return -1;
                default:
                    if (buf[0] == 0x1d) {
                        if (r->verbose)
                            fprintf(stderr, "\n" APPNAME ": received ^], exiting\n");
                        redir_sol_stop(r);
                    }
                    for (i = 0; i < rc; i++) {
                        /* meet BIOS expectations */
                        if (buf[i] == 0x0a)
                            buf[i] = 0x0d;
                    }
                    if (-1 == redir_sol_send(r, buf, rc))
                        return -1;
                    break;
            }
        }

        if (FD_ISSET(r->sock,&set)) {
            if (-1 == redir_data(r))
                return -1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */

struct termios  saved_attributes;
int             saved_fl;

static void tty_save(void)
{
    fcntl(STDIN_FILENO,F_GETFL,&saved_fl);
    tcgetattr (STDIN_FILENO, &saved_attributes);
}

static void tty_noecho(void)
{
    struct termios tattr;

    memcpy(&tattr,&saved_attributes,sizeof(struct termios));
    tattr.c_lflag &= ~(ECHO);
    tcsetattr (STDIN_FILENO, TCSAFLUSH, &tattr);
}

static void tty_raw(void)
{
    struct termios tattr;

    fcntl(STDIN_FILENO,F_SETFL,O_NONBLOCK);
    memcpy(&tattr,&saved_attributes,sizeof(struct termios));
    tattr.c_lflag &= ~(ISIG|ICANON|ECHO);
    tattr.c_cc[VMIN] = 1;
    tattr.c_cc[VTIME] = STDIN_FILENO;
    tcsetattr (STDIN_FILENO, TCSAFLUSH, &tattr);
}

static void tty_restore(void)
{
    fcntl(STDIN_FILENO,F_SETFL,saved_fl);
    saved_attributes.c_lflag &= (ECHO);
    tcsetattr (STDIN_FILENO, TCSANOW, &saved_attributes);
    

}

/* ------------------------------------------------------------------ */

static void usage(FILE *fp)
{
    fprintf(fp,
            "\n"
            "This is " APPNAME ", release " VERSION ", I'll establish\n"
            "serial-over-lan (sol) connections to your Intel AMT boxes.\n"
#if defined(USE_OPENSSL) || defined(USE_GNUTLS)
            "  - Support for AMT with TLS is available\n"
            "    with the -C command-line argument.\n"
            "  - Support for AMT with mutual TLS is also available\n"
            "    with the -c and -k arguments\n"
#endif
            "\n"
            "usage: " APPNAME " [options] host [port]\n"
            "options:\n"
            "   -d            debug output\n"
            "   -h            print this text\n"
            "   -v            verbose (default)\n"
            "   -q            quiet\n"
            "   -L            use legacy authentication\n"
#if defined(USE_OPENSSL) || defined(USE_GNUTLS)
            "   -A            enable SSL and use System Trust Store\n"
            "   -N            Allow insecure server connections\n"
            "   -C cacert     enable SSL and use PEM cacert file\n"
            "   [-c clientcert -k clientkey[:pass] ]  client certificate and key/pass \n"
#endif
            "   -u user       username (default: admin)\n"
            "   -p pass       password (default: $AMT_PASSWORD)\n"
            "\n"
#if defined(USE_OPENSSL) || defined(USE_GNUTLS)
            "By default port 16994 (SSL: 16995) is used.\n"
#else
            "By default port 16994 is used.\n"
#endif
            "If no password is given " APPNAME " will ask for one.\n"
            "\n"
            "-- \n"
            "(c) 2007 Gerd Hoffmann <kraxel@redhat.com>\n"
            "\n");
}

int main(int argc, char *argv[])
{
    struct redir r;
    char *h;
    int c;
    char *colon;

    memset(&r, 0, sizeof(r));
    r.verbose = 0;
    r.untrusted = 0;
    memcpy(r.type, "SOL ", 4);
    *(r.user) = '\0';
    *(r.privateKeyPassPhrase) = '\0';
    r.cb_data  = &r;
    r.cb_recv  = recv_tty;
    r.cb_state = state_tty;

    if (NULL != (h = getenv("AMT_PASSWORD")))
        snprintf(r.pass, sizeof(r.pass), "%s", h);

    for (;;) {
        if (-1 == (c = getopt(argc, argv, "AhvNdqu:p:LC:c:k:")))
            break;
        switch (c) {
            case 'v':
                r.verbose = 1;
                break;
            case 'd':
                r.trace = 1;
                break;
            case 'q':
                r.verbose = 0;
                break;
            case 'u':
                snprintf(r.user, sizeof(r.user), "%s", optarg);
                break;
            case 'p':
                snprintf(r.pass, sizeof(r.pass), "%s", optarg);
                memset(optarg,'*',strlen(optarg)); /* rm passwd from ps list */
                break;
            case 'L':
                r.legacy = 1;
                break;
#if defined(USE_OPENSSL) || defined(USE_GNUTLS)
            case 'N':
                r.untrusted = 1;
                r.cacert = NULL;
                break;
            case 'A':
                r.cacert = "ABC";
                break;
            case 'C':
                r.cacert = optarg;
                break;
            case 'c':
                r.clientcert = optarg;
                break;
            case 'k':
                colon = strrchr(optarg, ':');
                if (colon != NULL) {
                    strcpy(r.privateKeyPassPhrase, colon+1);
                    *colon='\0';
                }
                r.clientkey = optarg;
                break;
#endif

            case 'h':
                usage(stdout);
                exit(0);
            default:
                usage(stderr);
                exit(1);
        }
    }

    if (r.cacert != NULL)
    {
        if (((r.clientkey == NULL) && (r.clientkey != NULL)) ||
            ((r.clientkey != NULL) && (r.clientkey == NULL)))
        {
            fprintf(stderr, "Must provide both client certificate and key!");
            exit(1);
        }
    }
    if (optind < argc)   snprintf(r.host, sizeof(r.host), "%s", argv[optind]);
    if (optind+1 < argc) snprintf(r.port, sizeof(r.port), "%s", argv[optind+1]);
    if (0 == strlen(r.host)) {
        usage(stderr);
        exit(1);
    }

    if (0 == strlen(r.user)) {
        fprintf(stderr, "AMT Username [admin] for host %s: ", r.host);
        fgets(r.user, sizeof(r.user), stdin);
        if (NULL != (h = strchr(r.user, '\r')))
            *h = 0;
        if (NULL != (h = strchr(r.user, '\n')))
            *h = 0;
        if (strlen(r.user) == 0) strcpy(r.user,"admin");
    }

    tty_save();
    if (0 == strlen(r.pass)) {
        tty_noecho();
        fprintf(stderr, "AMT password for username %s on host %s: ", r.user, r.host);
        fgets(r.pass, sizeof(r.pass), stdin);
        tty_restore();
        fprintf(stderr, "\n");
        if (NULL != (h = strchr(r.pass, '\r')))
            *h = 0;
        if (NULL != (h = strchr(r.pass, '\n')))
            *h = 0;
    }

    if (r.clientkey != NULL && (strlen(r.privateKeyPassPhrase) == 0)) {
        tty_noecho();
        fprintf(stderr, "Private Key Password [enter for no pass phrase]: ");
        fgets(r.privateKeyPassPhrase, sizeof(r.privateKeyPassPhrase), stdin);
        tty_restore();
        fprintf(stderr, "\n");
        if (NULL != (h = strchr(r.privateKeyPassPhrase, '\r')))
            *h = 0;
        if (NULL != (h = strchr(r.privateKeyPassPhrase, '\n')))
            *h = 0;
        if (strlen(r.privateKeyPassPhrase) == 0) {
            fprintf(stderr, "Assuming unencrypted keystore.\n");
            r.privateKeyPassPhrase[0] = '\0';
        }
    }
            

    if (-1 == redir_connect(&r)) {
        tty_restore();
        exit(1);
    }

    tty_noecho();
    tty_raw();
    redir_start(&r);
    redir_loop(&r);
    tty_restore();

    exit(0);
}
