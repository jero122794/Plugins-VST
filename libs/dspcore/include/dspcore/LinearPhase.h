#pragma once
//
// Diseño de FIR de fase lineal por muestreo en frecuencia + ventana.
//
// Se evalúa la magnitud deseada en N/2+1 frecuencias, con fase cero; la IFFT
// da una respuesta simétrica que se centra en N/2 y se enventana (Blackman)
// para reducir el rizado por truncamiento. Retardo de grupo constante = N/2.
//
#include <cmath>
#include <complex>
#include <functional>
#include <vector>

namespace dspcore
{

/** FFT compleja in-place, radix-2 iterativa. inverse = true aplica también 1/N. */
inline void fft (std::vector<std::complex<double>>& a, bool inverse)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    constexpr double pi = 3.14159265358979323846;
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = 2.0 * pi / (double) len * (inverse ? 1.0 : -1.0);
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
    if (inverse)
        for (auto& x : a) x /= (double) n;
}

/**
    @param magnitudeDb  magnitud deseada (dB) en función de la frecuencia (Hz)
    @param numTaps      potencia de 2
    @returns FIR de numTaps coeficientes, con el pico en numTaps/2
*/
inline std::vector<float> designLinearPhaseFir (const std::function<double (double)>& magnitudeDb, double sampleRate, int numTaps)
{
    constexpr double pi = 3.14159265358979323846;
    const size_t n = (size_t) numTaps;
    std::vector<std::complex<double>> spec (n);
    for (size_t k = 0; k <= n / 2; ++k)
    {
        const double f = (double) k * sampleRate / (double) n;
        const double mag = std::pow (10.0, magnitudeDb (std::max (f, 1.0)) / 20.0);
        spec[k] = mag;
        if (k > 0 && k < n / 2) spec[n - k] = mag;   // simetría hermítica (fase cero → real)
    }
    fft (spec, true);

    std::vector<float> h (n);
    for (size_t i = 0; i < n; ++i)
    {
        const size_t src = (i + n / 2) % n;        // centrar en n/2
        const double w = 0.42 - 0.5 * std::cos (2.0 * pi * (double) i / (double) n)
                              + 0.08 * std::cos (4.0 * pi * (double) i / (double) n);   // Blackman
        h[i] = (float) (spec[src].real() * w);
    }
    return h;
}

} // namespace dspcore
