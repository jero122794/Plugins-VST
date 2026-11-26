#pragma once
//
// Etapas de salida. Se ejecutan sobremuestreadas en el wrapper.
//
//  - Tube (LA-2A): 12AX7/12BH7 + transformador. Curva asimétrica (armónicos pares,
//    sobre todo el 2º) y saturación del transformador concentrada en graves.
//  - SolidState (LA-3A): clase A a transistores + transformador. Curva casi
//    simétrica (predomina el 3º), más limpia, con menos saturación de graves.
//
// "color" ∈ [0,1]: 0 = transparente. El hardware no tiene este control; es un
// añadido práctico.
//
#include "OnePole.h"
#include <cmath>

namespace dspcore
{

enum class StageType { Tube = 0, SolidState = 1 };

class OutputStage
{
public:
    void prepare (double sampleRate) noexcept
    {
        xfmrLp.prepare (sampleRate, 120.0f);
        dc.prepare (sampleRate, 8.0f);
    }

    void reset() noexcept
    {
        xfmrLp.reset();
        dc.reset();
    }

    void setType (StageType t) noexcept { type = t; }
    void setColor (float c) noexcept    { color = std::fmax (0.0f, std::fmin (1.0f, c)); }

    float process (float x) noexcept
    {
        if (color <= 0.0f)
            return x;

        float y = type == StageType::Tube ? tube (x) : solidState (x);

        // Transformador: solo satura la parte grave de la señal.
        const float xfmrDrive = type == StageType::Tube ? 2.5f : 1.5f;
        const float lf = xfmrLp.process (y);
        y = (y - lf) + std::tanh (lf * xfmrDrive) / xfmrDrive;

        y = dc.process (y);  // la asimetría genera DC
        return x + color * (y - x);
    }

private:
    static float tube (float x) noexcept
    {
        // tanh con bias: asimetría → 2º armónico. Normalizada a ganancia unidad en pequeña señal.
        constexpr float k = 1.2f, b = 0.15f;
        const float tb = std::tanh (k * b);
        const float norm = k * (1.0f - tb * tb);
        return (std::tanh (k * (x + b)) - tb) / norm;
    }

    static float solidState (float x) noexcept
    {
        // Simétrica y blanda, con una asimetría mínima.
        constexpr float k = 1.2f, b = 0.04f;
        const float tb = std::tanh (k * b);
        const float norm = k * (1.0f - tb * tb);
        return (std::tanh (k * (x + b)) - tb) / norm;
    }

    StageType type = StageType::Tube;
    float color = 0.0f;
    OnePoleLowpass xfmrLp;
    DcBlocker dc;
};

} // namespace dspcore
