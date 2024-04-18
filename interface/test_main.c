#include <libconfig.h>

int main(int argc, char *argv[]){
    config_t cfg;
    config_init(&cfg);
    config_read_file(&cfg, "config.cfg");

    /* Read the file. If there is an error, report it and exit. */
    if (!config_read_file(&cfg, "example.cfg"))
    {
        fprintf(stderr, "%s:%d - %s\n", config_error_file(&cfg),
                config_error_line(&cfg), config_error_text(&cfg));
        config_destroy(&cfg);
        return -1;
    }

    return 0;
}