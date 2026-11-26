#pragma once
//
// Filtro de variable de estado TPT ("trapezoidal integration", Zavalishin / Simper).
//
// Por qué SVF y no biquads en forma directa: en un EQ dinámico la ganancia
// cambia continuamente. Un biquad en forma directa modulado produce
// transitorios e incluso inestabilidad; el SVF TPT se mantiene estable y
// limpio con modulación muestra a muestra.
//
// La salida es una mezcla lineal: y = m0·x + m1·band + m2·low.
// Su respuesta es exactamente la del prototipo analógico con transformada
// bilineal prewarpeada, lo que permite dibujar en la UI la curva EXACTA
// que se está aplicando al audio (ver response()).
//
#include <cmath>
#include <complex>

namespace dspcore
{

enum class FilterShape { Bell, LowShelf, HighShelf, LowPass, HighPass, Notch, BandPass };

struct SvfCoeffs
{
    float g = 0, k = 1, a1 = 1, a2 = 0, a3 = 0;
    float m0 = 1, m1 = 0, m2 = 0;

    static SvfCoeffs design (FilterShape shape, double freq, double sampleRate, double q, double gainDb) noexcept
    {
        constexpr double pi = 3.14159265358979323846;
        freq = std::fmin (std::fmax (freq, 1.0), sampleRate * 0.4995);
        q = std::fmax (q, 0.025);
        const double A = std::pow (10.0, gainDb / 40.0);
        double g = std::tan (pi * freq / sampleRate);
        double k = 1.0 / q;
        double m0 = 1, m1 = 0, m2 = 0;

        switch (shape)
        {
            case FilterShape::Bell:      k = 1.0 / (q * A); m0 = 1; m1 = k * (A * A - 1.0); m2 = 0; break;
            case FilterShape::LowShelf:  g /= std::sqrt (A); m0 = 1; m1 = k * (A - 1.0); m2 = A * A - 1.0; break;
            case FilterShape::HighShelf: g *= std::sqrt (A); m0 = A * A; m1 = k * (1.0 - A) * A; m2 = 1.0 - A * A; break;
            case FilterShape::LowPass:   m0 = 0; m1 = 0;  m2 = 1;  break;
            case FilterShape::HighPass:  m0 = 1; m1 = -k; m2 = -1; break;
            case FilterShape::Notch:     m0 = 1; m1 = -k; m2 = 0;  break;
            case FilterShape::BandPass:  m0 = 0; m1 = k;  m2 = 0;  break; // ganancia 1 en el centro
        }

        SvfCoeffs c;
        c.g = (float) g;
        c.k = (float) k;
        const double a1 = 1.0 / (1.0 + g * (g + k));
        c.a1 = (float) a1;
        c.a2 = (float) (g * a1);
        c.a3 = (float) (g * g * a1);
        c.m0 = (float) m0;
        c.m1 = (float) m1;
        c.m2 = (float) m2;
        return c;
    }

    /** Respuesta compleja en la frecuencia digital w = 2π·f/fs. */
    std::complex<double> response (double w) const noexcept
    {
        const std::complex<double> s (0.0, std::tan (0.5 * w) / (double) g);  // s normalizado (bilineal)
        const auto den = s * s + (double) k * s + 1.0;
        const auto band = s / den;
        const auto low = 1.0 / den;
        return (double) m0 + (double) m1 * band + (double) m2 * low;
    }
};

class Svf
{
public:
    void reset() noexcept { ic1 = ic2 = 0.0f; }

    float process (float x, const SvfCoeffs& c) noexcept
    {
        const float v3 = x - ic2;
        const float v1 = c.a1 * ic1 + c.a2 * v3;
        const float v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        return c.m0 * x + c.m1 * v1 + c.m2 * v2;
    }

private:
    float ic1 = 0.0f, ic2 = 0.0f;
};

} // namespace dspcore
