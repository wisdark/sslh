#include <libconfig.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "common.h"


#define print_message(sink, format, file, line) fprintf(stderr, format, file, line)

static char* resolve_listen(const char *hostname, const char *port) {
    /* Need room in the strcat for \0 and :
     * the format in the socket unit file is hostname:port */
    char *conn = malloc(strlen(hostname)+strlen(port)+2);
    CHECK_ALLOC(conn, "malloc");
    strcpy(conn, hostname);
    strcat(conn, ":");
    strcat(conn, port);

    return conn;

}


static int get_listen_from_conf(const char *filename, char **listen[]) {
    config_t config;
    config_setting_t *setting, *addr;
    const char *hostname, *port;
    int len = 0;

    /* look up the listen stanzas in the config file so these
     * can be used in the socket file generated */
    config_init(&config);
    if (config_read_file(&config, filename) == CONFIG_FALSE) {
        /* we don't care if file is missing, skip it */
        if (config_error_line(&config) != 0) {
            fprintf(stderr, "%s:%d:%s\n",
                    filename,
                    config_error_line(&config),
                    config_error_text(&config));
            return -1;
        }
    } else {
        setting = config_lookup(&config, "listen");
        if (setting) {
	    int i;
            len = config_setting_length(setting);
            *listen = malloc(len * sizeof(**listen));
            CHECK_ALLOC(*listen, "malloc");
            for (i = 0; i < len; i++) {
                addr = config_setting_get_elem(setting, i);
                if (! (config_setting_lookup_string(addr, "host", &hostname) &&
                       config_setting_lookup_string(addr, "port", &port))) {
                    fprintf(stderr,
                            "line %d:Incomplete specification (hostname and port required)\n",
                            config_setting_source_line(addr));
                    return -1;
                } else {
                    char *resolved_listen = resolve_listen(hostname, port);

                    (*listen)[i] = malloc(strlen(resolved_listen));
                    CHECK_ALLOC((*listen)[i], "malloc");
                    strcpy((*listen)[i], resolved_listen);
                    free(resolved_listen);
                }
            }
        }
    }

    return len;

}

static int write_socket_unit(FILE *socket, char *listen[], int num_addr, const char *source) {
    int i;

    fprintf(socket,
            "# Automatically generated by systemd-sslh-generator\n\n"
            "[Unit]\n"
            "Before=sslh.service\n"
            "SourcePath=%s\n"
            "Documentation=man:sslh(8) man:systemd-sslh-generator(8)\n\n"
            "[Socket]\n"
            "FreeBind=true\n",
            source);

    for (i = 0; i < num_addr; i++) {
        fprintf(socket, "ListenStream=%s\n", listen[i]);
    }

    return 0;
}

static int gen_sslh_config(char *runtime_unit_dir) {
    char *sslh_conf;
    int num_addr;
    FILE *config;
    char **listen;
    FILE *runtime_conf_fd = stdout;
    const char *unit_file;

    /* There are two default locations so check both with first given preference */
    sslh_conf = "/etc/sslh.cfg";

    config = fopen(sslh_conf, "r");
    if (config  == NULL) {
        sslh_conf="/etc/sslh/sslh.cfg";
        config = fopen(sslh_conf, "r");
        if (config == NULL) {
            return -1;
        }
    }

    fclose(config);

    num_addr = get_listen_from_conf(sslh_conf, &listen);
    if (num_addr < 0)
        return -1;

    /* If this is run by systemd directly write to the location told to
     * otherwise write to standard out so that it's trivial to check what
     * will be written */
    if (runtime_unit_dir && *runtime_unit_dir) {
        unit_file = "/sslh.socket";
        size_t uf_len = strlen(unit_file);
        size_t runtime_len = strlen(runtime_unit_dir) + uf_len + 1;
        char *runtime_conf = malloc(runtime_len);
        CHECK_ALLOC(runtime_conf, "malloc");
        strcpy(runtime_conf, runtime_unit_dir);
        strcat(runtime_conf, unit_file);
        runtime_conf_fd = fopen(runtime_conf, "w");
        free(runtime_conf);
    }


    return write_socket_unit(runtime_conf_fd, listen, num_addr, sslh_conf);
}


int main(int argc, char *argv[]){
    int r = 0;
    int k;
    char *runtime_unit_dest = "";

    if (argc > 1 && (argc != 4) ) {
        printf("This program takes three or no arguments.\n");
        return -1;
    }

    if (argc  > 1)
        runtime_unit_dest = argv[1];

    k = gen_sslh_config(runtime_unit_dest);
    if (k < 0)
        r = k;

    return r < 0 ? -1 : 0;
}


