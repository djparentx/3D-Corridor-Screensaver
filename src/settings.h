#ifndef SETTINGS_H
#define SETTINGS_H

struct settings {
    float speed;     /* 0.5 - 1.5, 0.1 steps    default 1.0 */
    int   ambient;   /* 0 - 100                 default 100 */
    int   panels;    /* 1 - 5 x dead panels     default 1   */
    int   flicker;   /* 1 - 5 x flicker panels  default 1   */
    int   fog;       /* 1 - 5 x fog density     default 1   */
    int   volume;    /* 0 - 5, 0 off, 3 = 0 dB  default 3   */
};

void settings_parse(struct settings *s, int argc, char **argv);
void settings_print(const struct settings *s);

#endif
