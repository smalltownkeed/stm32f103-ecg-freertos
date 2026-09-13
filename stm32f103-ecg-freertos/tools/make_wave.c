/* Optional table generator: make_wave.exe > waveform.h
 * The third lead is derived after rounding, so II - I - III is exactly zero.
 */
#include <math.h>
#include <stdio.h>

static double pulse(double time, double center, double width)
{
    double distance = (time - center) / width;
    return exp(-0.5 * distance * distance);
}

int main(void)
{
    puts("/* Educational PQRST waveform; PWM counts, no clinical calibration. */");
    puts("#pragma once\n#include <stdint.h>\n#define ECG_WAVE_LENGTH 200u\nstatic const uint8_t ecg_wave[200][3] = {");
    for (unsigned i = 0; i < 200; i++) {
        double t = i / 250.0;
        int lead_i = (int)lround(8*pulse(t,.12,.025) - 11*pulse(t,.235,.009) + 66*pulse(t,.26,.012)
                                 - 17*pulse(t,.29,.012) + 19*pulse(t,.47,.052));
        int lead_ii = (int)lround(11*pulse(t,.12,.028) - 9*pulse(t,.232,.01) + 87*pulse(t,.263,.014)
                                  - 23*pulse(t,.295,.014) + 27*pulse(t,.48,.058));
        printf("    {%d, %d, %d},\n", 128 + lead_i, 128 + lead_ii, 128 + lead_ii - lead_i);
    }
    puts("};");
    return 0;
}
