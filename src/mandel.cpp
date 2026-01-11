#include "mandel.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace mandel
{

ColorScheme::ColorScheme(int count)
{
    const int n = (count > 0) ? count : 1;
    palette.clear();
    palette.reserve(static_cast<::std::size_t>(n));

    constexpr float two_pi = 6.2831853071795864769f;
    constexpr float phase_r = 0.0f;
    constexpr float phase_g = two_pi / 3.0f;
    constexpr float phase_b = 2.0f * two_pi / 3.0f;

    const auto to_u8 = [](const float x) -> ::std::uint8_t
    {
        const float clamped = ::std::clamp(x, 0.0f, 1.0f);
        return static_cast<::std::uint8_t>(::std::lround(clamped * 255.0f));
    };

    for (int i = 0; i < n; ++i)
    {
        const float t = (static_cast<float>(i) / static_cast<float>(n)) * two_pi;
        const float rf = 0.5f + 0.5f * ::std::sinf(t + phase_r);
        const float gf = 0.5f + 0.5f * ::std::sinf(t + phase_g);
        const float bf = 0.5f + 0.5f * ::std::sinf(t + phase_b);
        palette.push_back(Color{to_u8(rf), to_u8(gf), to_u8(bf)});
    }
}

}  // namespace mandel
