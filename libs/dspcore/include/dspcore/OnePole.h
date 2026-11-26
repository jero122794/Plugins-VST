#pragma once
//
// Bloques básicos: coeficientes de un polo, filtros de un polo y DC blocker.
// C++ puro, sin dependencias de JUCE, para poder testearlo fuera del DAW.
//
#include <cmath>

namespace dspcore
{

/** Coeficiente de un filtro de un polo para una constante de tiempo tau (ms). */
inline float onePoleCoef (float tauMs, double sampleRate) noexcept
{
    if (tauMs <= 0.0f)
        return 0.0f;
    return (float) std::exp (-1.0 / (0.001 * (double) tauMs * sampleRate));
}

/** Low-pass de un polo (6 dB/oct), parametrizado por frecuencia de corte. */
class OnePoleLowpass
{
public:
    void prepare (double sampleRate, float cutoffHz) noexcept
    {
        a = (float) std::exp (-2.0 * 3.14159265358979323846 * (double) cutoffHz / sampleRate);
        reset();
    }

    void reset() noexcept { z = 0.0f; }

    float process (float x) noexcept
    {
        z = x + a * (z - x);
        return z;
    }

private:
    float a = 0.0f, z = 0.0f;
};

/** Bloqueador de DC (HPF de un polo + un cero en DC). */
class DcBlocker
{
public:
    void prepare (double sampleRate, float cutoffHz = 8.0f) noexcept
    {
        r = (float) std::exp (-2.0 * 3.14159265358979323846 * (double) cutoffHz / sampleRate);
        reset();
    }

    void reset() noexcept { x1 = y1 = 0.0f; }

    float process (float x) noexcept
    {
        const float y = x - x1 + r * y1;
        x1 = x;
        y1 = y;
        return y;
    }

private:
    float r = 0.999f, x1 = 0.0f, y1 = 0.0f;
};

/** Evita denormales en estados que decaen hacia cero. */
inline float flushDenormal (float v) noexcept
{
    return std::fabs (v) < 1.0e-15f ? 0.0f : v;
}

} // namespace dspcore
