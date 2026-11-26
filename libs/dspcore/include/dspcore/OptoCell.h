#pragma once
//
// Modelo de la célula óptica T4 (panel electroluminiscente + fotorresistencia).
//
// Lo que hace que un LA-2A/LA-3A suene como suena no es el umbral ni el ratio,
// sino el comportamiento temporal de la célula:
//
//   1. El panel EL convierte la señal de sidechain en luz con un pequeño retraso.
//   2. La fotorresistencia (LDR) reacciona rápido cuando la luz sube (ataque)...
//   3. ...pero se recupera en dos fases: ~50 % en ~60 ms y el resto en
//      0.5–15 s, MÁS LENTO cuanto más tiempo y más fuerte se haya comprimido
//      (efecto "memoria" del material fotosensible).
//
// Aquí se modela como:
//   - conductancia g = (1-w)·gFast + w·gSlow  (adimensional)
//   - ganancia del atenuador = 1 / (1 + g)    (divisor resistivo con la LDR a masa)
//   - la constante de release de gSlow se interpola según un estado de memoria m ∈ [0,1].
//
#include "OnePole.h"
#include <algorithm>
#include <cmath>

namespace dspcore
{

struct OptoCellParams
{
    float elAttackMs;         // ataque efectivo: se filtra la LUZ, no la conductancia (ver nota abajo)
    float elReleaseMs;        // respuesta del panel EL cuando baja
    float attackMs;           // inercia residual de la LDR al bajar su resistencia
    float fastReleaseMs;      // tau de la primera fase de release
    float slowReleaseMinMs;   // tau de la segunda fase tras una compresión corta
    float slowReleaseMaxMs;   // tau de la segunda fase tras una compresión larga
    float memoryChargeMs;     // cuánto tarda en "cargarse" la memoria
    float memoryDischargeMs;  // cuánto tarda en olvidarse
    float slowWeight;         // proporción de la conductancia en la fase lenta

    // Nota sobre el ataque: en topología feedback, al empezar a comprimir la
    // conductancia objetivo puede ser ~20x la de equilibrio. Si el ataque se
    // filtrara sobre la conductancia llegaría al equilibrio en una fracción de su
    // tau. Filtrando la luz (antes de la ley de potencia) el sobreimpulso es ~2-3x
    // y elAttackMs se corresponde con el ataque medido.
    //
    // Valores de partida. [Probable] según especificaciones publicadas de los equipos
    // (LA-2A: ataque 10 ms, release 60 ms al 50 % y 1–15 s total). Los del LA-3A
    // son más aproximados: hay que ajustarlos contra mediciones de un equipo real.
    static OptoCellParams la2a() noexcept { return { 10.0f, 15.0f, 2.0f, 25.0f, 500.0f, 5000.0f, 2000.0f, 8000.0f, 0.5f }; }
    static OptoCellParams la3a() noexcept { return { 2.5f, 10.0f, 0.5f, 25.0f, 250.0f, 1800.0f, 1500.0f, 5000.0f, 0.5f }; }
};

class OptoCell
{
public:
    void prepare (double sampleRate, const OptoCellParams& p) noexcept
    {
        fs = sampleRate;
        setParams (p);
        reset();
    }

    void setParams (const OptoCellParams& p) noexcept
    {
        params    = p;
        elA       = onePoleCoef (p.elAttackMs, fs);
        elR       = onePoleCoef (p.elReleaseMs, fs);
        attA      = onePoleCoef (p.attackMs, fs);
        fastR     = onePoleCoef (p.fastReleaseMs, fs);
        memCharge = onePoleCoef (p.memoryChargeMs, fs);
        memDisch  = onePoleCoef (p.memoryDischargeMs, fs);
        logTauMin = std::log (p.slowReleaseMinMs);
        logTauMax = std::log (p.slowReleaseMaxMs);
    }

    void reset() noexcept { el = gFast = gSlow = memory = 0.0f; }

    /**
        @param drive     nivel de sidechain rectificado y ya escalado por Peak Reduction
        @param exponent  respuesta de la LDR a la luz; fija el ratio efectivo en
                         topología feedback (ratio ≈ 1 + exponent)
        @returns conductancia g (adimensional). Ganancia = 1 / (1 + g).
    */
    float process (float drive, float exponent) noexcept
    {
        // 1) Panel EL
        const float elCoef = drive > el ? elA : elR;
        el = flushDenormal (drive + elCoef * (el - drive));

        // 2) Conductancia objetivo de la LDR (ley de potencia)
        const float target = el > 0.0f ? std::min (std::pow (el, exponent), 1.0e4f) : 0.0f;

        // 3) Memoria: se carga con compresión sostenida y se descarga despacio
        const float memTarget = target / (target + 1.0f);
        const float memCoef = memTarget > memory ? memCharge : memDisch;
        memory = flushDenormal (memTarget + memCoef * (memory - memTarget));

        // 4) Fase rápida
        gFast = flushDenormal (target + (target > gFast ? attA : fastR) * (gFast - target));

        // 5) Fase lenta, con release dependiente del programa
        float slowCoef = attA;
        if (target <= gSlow)
        {
            const float tau = std::exp (logTauMin + memory * (logTauMax - logTauMin));
            slowCoef = onePoleCoef (tau, fs);
        }
        gSlow = flushDenormal (target + slowCoef * (gSlow - target));

        return (1.0f - params.slowWeight) * gFast + params.slowWeight * gSlow;
    }

    float getMemory() const noexcept { return memory; }

private:
    double fs = 48000.0;
    OptoCellParams params = OptoCellParams::la2a();
    float elA = 0, elR = 0, attA = 0, fastR = 0, memCharge = 0, memDisch = 0;
    float logTauMin = 0, logTauMax = 0;
    float el = 0, gFast = 0, gSlow = 0, memory = 0;
};

} // namespace dspcore
