#include "daynight.h"

int g_daytime = 1000;
static int s_sub;

void daynight_set(int t) { g_daytime = ((t % 24000) + 24000) % 24000; s_sub = 0; }

void daynight_tick(void)
{
    if (++s_sub >= 3) {
        s_sub = 0;
        if (++g_daytime >= 24000) g_daytime = 0;
    }
}

#define LIGHT_MIN 12
#define LIGHT_MAX 31

// transicion suave (smoothstep) 0..256 para x en 0..2000
static int smooth(int x)
{
    int u = x * 256 / 2000;
    return (u * u * (768 - 2 * u)) >> 16;
}

int daynight_light(void)
{
    int t = g_daytime;
    if (t < 12000) return LIGHT_MAX;
    if (t < 14000) return LIGHT_MAX - (LIGHT_MAX - LIGHT_MIN) * smooth(t - 12000) / 256;   // atardecer
    if (t < 22000) return LIGHT_MIN;                                                         // noche
    return LIGHT_MIN + (LIGHT_MAX - LIGHT_MIN) * smooth(t - 22000) / 256;                   // amanecer
}

int daynight_is_night(void) { return daynight_light() <= 15; }

// 0..256: intensidad del tinte naranja (maxima a mitad del atardecer / amanecer)
static int twilight(void)
{
    int t = g_daytime, u;
    if (t >= 12000 && t < 14000) u = (t - 12000) * 256 / 2000;
    else if (t >= 22000) u = (t - 22000) * 256 / 2000;
    else return 0;
    return 4 * u * (256 - u) / 256;
}

// Color del cielo = color de la niebla (horizonte claro en el dia, naranja al atardecer / amanecer)
void daynight_sky(int *r, int *g, int *b)
{
    int l = daynight_light() - LIGHT_MIN;      // 0..19
    int span = LIGHT_MAX - LIGHT_MIN;
    // dia (17, 23, 31) -> noche (1, 2, 5)
    int rr = 1 + (17 - 1) * l / span;
    int gg = 2 + (23 - 2) * l / span;
    int bb = 5 + (31 - 5) * l / span;
    int k = twilight() * 150 >> 8;             // 0..150 (de 256)
    *r = rr + (((28 - rr) * k) >> 8);
    *g = gg + (((14 - gg) * k) >> 8);
    *b = bb + (((9 - bb) * k) >> 8);
}
