#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "settings.h"

static int parse_int(const char *name, const char *val, int lo, int hi, int def)
{
    char *end;
    long v;

    if (!val || !*val) {
        printf("%-14s %s missing value, using %d\n", "ARG WARNING", name, def);
        return def;
    }
    v = strtol(val, &end, 10);
    if (*end != '\0') {
        printf("%-14s %s '%s' invalid, using %d\n", "ARG WARNING", name, val, def);
        return def;
    }
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    return (int)v;
}

static float parse_speed(const char *val, float def)
{
    char *end;
    double v;

    if (!val || !*val) {
        printf("%-14s --speed missing value, using %.1f\n", "ARG WARNING", def);
        return def;
    }
    v = strtod(val, &end);
    if (*end != '\0') {
        printf("%-14s --speed '%s' invalid, using %.1f\n", "ARG WARNING", val, def);
        return def;
    }
    v = floor(v * 10.0 + 0.5) / 10.0;
    if (v < 0.5) v = 0.5;
    if (v > 1.5) v = 1.5;
    return (float)v;
}

void settings_parse(struct settings *s, int argc, char **argv)
{
    int i;

    s->speed      = 1.0f;
    s->ambient    = 100;
    s->panels     = 1;
    s->flicker    = 1;
    s->fog        = 1;
    s->volume     = 3;
    s->brightness = 100;

    for (i = 1; i < argc; i++) {
        const char *a   = argv[i];
        const char *val = i + 1 < argc ? argv[i + 1] : NULL;

        if (!strcmp(a, "--speed")) {
            s->speed = parse_speed(val, s->speed);
            i++;
        } else if (!strcmp(a, "--ambient")) {
            s->ambient = parse_int(a, val, 0, 100, s->ambient);
            i++;
        } else if (!strcmp(a, "--panels")) {
            s->panels = parse_int(a, val, 1, 5, s->panels);
            i++;
        } else if (!strcmp(a, "--flicker")) {
            s->flicker = parse_int(a, val, 1, 5, s->flicker);
            i++;
        } else if (!strcmp(a, "--fog")) {
            s->fog = parse_int(a, val, 1, 5, s->fog);
            i++;
        } else if (!strcmp(a, "--volume")) {
            s->volume = parse_int(a, val, 0, 5, s->volume);
            i++;
        } else if (!strcmp(a, "--brightness")) {
            s->brightness = parse_int(a, val, 0, 100, s->brightness);
            i++;
        } else {
            printf("%-14s unknown argument '%s'\n", "ARG WARNING", a);
        }
    }
}

void settings_print(const struct settings *s)
{
    printf("%-14s %.1f\n", "Speed",      s->speed);
    printf("%-14s %d\n",   "Ambient",    s->ambient);
    printf("%-14s %d\n",   "Panels",     s->panels);
    printf("%-14s %d\n",   "Flicker",    s->flicker);
    printf("%-14s %d\n",   "Fog",        s->fog);
    printf("%-14s %d\n",   "Volume",     s->volume);
    printf("%-14s %d\n",   "Brightness", s->brightness);
}