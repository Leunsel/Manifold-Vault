#include "UI/LockScene.h"

#include "App/BunkerTheme.h"
#include "UI/UiMath.h"
#include "UI/UiScale.h"

#include <cmath>
#include <cstdint>

namespace Bunker::UI
{
    using Theme::Token;

    namespace
    {
        constexpr int kMaxColumns = 480;
        constexpr float kReferenceLand = 274.0f;
        constexpr float kCabinScale = 1.45f;

        float Phase(double t, double cyclesPerSecond, float offset = 0.0f)
        {
            return static_cast<float>(std::fmod(t * cyclesPerSecond, 1.0)) * kTwoPi + offset;
        }

        float Wave(double t, double cyclesPerSecond, float offset = 0.0f)
        {
            return std::sin(Phase(t, cyclesPerSecond, offset));
        }

        float Ease(float from, float to, float x)
        {
            if (std::fabs(to - from) < 0.001f)
                return x < from ? 0.0f : 1.0f;
            const float t = Clamp((x - from) / (to - from), 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        }

        float Bell(float x)
        {
            return std::exp(-x * x);
        }

        float Peak(float x)
        {
            return 1.0f - std::fabs(2.0f * (x - std::floor(x)) - 1.0f);
        }

        float Hash(std::uint32_t n)
        {
            n ^= n >> 16;
            n *= 0x7FEB352Du;
            n ^= n >> 15;
            n *= 0x846CA68Bu;
            n ^= n >> 16;
            return static_cast<float>(n & 0xFFFFFFu) / 16777216.0f;
        }

        ImVec4 Mix(ImVec4 a, ImVec4 b, float t)
        {
            return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, 1.0f);
        }

        ImU32 Tint(ImVec4 colour, float alpha)
        {
            return ImGui::ColorConvertFloat4ToU32(ImVec4(Clamp(colour.x, 0.0f, 1.0f), Clamp(colour.y, 0.0f, 1.0f),
                                                         Clamp(colour.z, 0.0f, 1.0f), Clamp(alpha, 0.0f, 1.0f)));
        }

        ImDrawIdx Index(unsigned int base, int offset)
        {
            return static_cast<ImDrawIdx>(base + static_cast<unsigned int>(offset));
        }

        // Quads between neighbouring columns of `perColumn` vertices each, stored column by column.
        void StripIndices(ImDrawList* dl, unsigned int base, int columns, int perColumn)
        {
            for (int col = 0; col < columns; ++col)
            {
                for (int k = 0; k < perColumn - 1; ++k)
                {
                    const int a = col * perColumn + k;
                    const int b = a + perColumn;
                    dl->PrimWriteIdx(Index(base, a));
                    dl->PrimWriteIdx(Index(base, b));
                    dl->PrimWriteIdx(Index(base, b + 1));
                    dl->PrimWriteIdx(Index(base, a));
                    dl->PrimWriteIdx(Index(base, b + 1));
                    dl->PrimWriteIdx(Index(base, a + 1));
                }
            }
        }

        struct Frame
        {
            ImVec2 Min;
            ImVec2 Max;
            float Unit = 1.0f;
            float MoonX = 0.0f;
            float MoonY = 0.0f;
            float Glow = 0.0f;
            float Water = 0.0f;
            float CabinX = 0.0f;
            double Time = 0.0;

            float Width() const { return Max.x - Min.x; }
            float U(float design) const { return design * Unit; }
            float Up(float design) const { return Max.y - design * Unit; }
            float Along(float x) const { return (x - MoonX) / (Unit * 1000.0f); }

            int Columns(float spacing) const
            {
                return Clamp(static_cast<int>(Width() / (spacing * Unit)), 48, kMaxColumns);
            }

            float ColumnX(int col, int columns) const
            {
                return Min.x + Width() * static_cast<float>(col) / static_cast<float>(columns);
            }
        };

        struct Tones
        {
            bool Light = false;
            ImVec4 Night, Cool, Warm, Rose, Dark;
            ImVec4 Moon, Mare, Halo, Sheen, Cloud, Smoke;
            ImVec4 Lamp, Ember;
            ImVec4 Haze, Water, Deep, Hill, Wall, Roof;
            ImVec4 Ridge[3], Mist[3];
            float CoolLift = 0.0f, WarmLift = 0.0f, RoseLift = 0.0f, MoonLift = 0.0f, HazeLift = 0.0f;
            float StarAlpha = 0.0f, HaloAlpha = 0.0f, CloudAlpha = 0.0f, CloudLit = 0.0f;
            float PathAlpha = 0.0f, MistAlpha = 0.0f, LampAlpha = 0.0f, GlowAlpha = 0.0f, SmokeAlpha = 0.0f;
        };

        ImVec4 SkyAt(const Frame& f, const Tones& c, float x, float y, float dim)
        {
            const float span = Max(f.Glow - f.Min.y, 1.0f);
            const float v = Clamp((y - f.Min.y) / span, 0.0f, 1.0f);
            const float side = Bell((x - f.MoonX) / (f.Width() * 0.45f));
            const float glow = Bell(Max(0.0f, f.Glow - y) / f.U(150.0f)) * (0.60f + 0.40f * side);
            const float mx = (x - f.MoonX) / f.U(300.0f);
            const float my = (y - f.MoonY) / f.U(240.0f);
            const float moon = c.MoonLift * std::exp(-(mx * mx + my * my)) + c.HazeLift * glow;
            const float cool = c.CoolLift * (0.25f + 0.75f * std::sqrt(v));
            const float rose = c.RoseLift * Bell((f.Glow - f.U(105.0f) - y) / f.U(80.0f)) * (0.4f + 0.6f * side);
            const float warm = c.WarmLift * Bell(Max(0.0f, f.Glow - y) / f.U(70.0f)) * side;
            const auto lift = [&](float night, float cl, float ro, float wa, float ha) {
                return night + dim * (cool * (cl - night) + rose * (ro - night) + warm * (wa - night) +
                                      moon * (ha - night));
            };
            return ImVec4(lift(c.Night.x, c.Cool.x, c.Rose.x, c.Warm.x, c.Sheen.x),
                          lift(c.Night.y, c.Cool.y, c.Rose.y, c.Warm.y, c.Sheen.y),
                          lift(c.Night.z, c.Cool.z, c.Rose.z, c.Warm.z, c.Sheen.z), 1.0f);
        }

        constexpr float kDarkDepth[4] = { 0.32f, 0.52f, 0.72f, 0.92f };
        constexpr float kLightDepth[4] = { 0.10f, 0.17f, 0.24f, 0.30f };

        Tones MakeTones(const Frame& f)
        {
            Tones c;
            c.Light = Theme::IsLight();
            c.Cool = Theme::Color(Token::GlowDim);
            c.Warm = Theme::Color(Token::WarnDim);
            c.Rose = Theme::Color(Token::RedDim);
            const ImVec4 text = Theme::Color(Token::TextPrimary);
            const ImVec4 shade = Theme::Color(Token::Shade);
            if (c.Light)
            {
                c.Night = Theme::Color(Token::Surface);
                c.Dark = shade;
                c.CoolLift = 0.70f;
                c.WarmLift = 0.45f;
                c.RoseLift = 0.30f;
                c.MoonLift = 0.40f;
                c.HazeLift = 0.50f;
                c.Moon = Theme::Color(Token::Panel);
                c.Halo = c.Moon;
                c.Sheen = c.Moon;
                c.Lamp = Mix(c.Warm, Theme::Color(Token::WarnCore), 0.35f);
                c.Ember = Mix(c.Warm, Theme::Color(Token::WarnCore), 0.55f);
                c.HaloAlpha = 0.45f;
                c.CloudAlpha = 0.55f;
                c.CloudLit = 0.3f;
                c.PathAlpha = 0.45f;
                c.MistAlpha = 0.55f;
                c.LampAlpha = 0.70f;
                c.GlowAlpha = 0.10f;
                c.SmokeAlpha = 0.16f;
            }
            else
            {
                c.Night = Theme::Color(Token::Void);
                c.Dark = Mix(shade, c.Cool, 0.10f);
                c.Warm = Mix(c.Warm, c.Rose, 0.30f);
                c.CoolLift = 0.55f;
                c.WarmLift = 0.12f;
                c.RoseLift = 0.0f;
                c.MoonLift = 0.07f;
                c.HazeLift = 0.13f;
                c.Lamp = Theme::Color(Token::WarnBright);
                c.Ember = Theme::Color(Token::WarnCore);
                c.Moon = Mix(text, c.Lamp, 0.38f);
                c.Halo = Mix(c.Moon, c.Lamp, 0.25f);
                c.Sheen = Mix(text, Theme::Color(Token::GlowCore), 0.40f);
                c.StarAlpha = 0.85f;
                c.HaloAlpha = 0.20f;
                c.CloudAlpha = 0.30f;
                c.CloudLit = 0.55f;
                c.PathAlpha = 0.55f;
                c.MistAlpha = 0.30f;
                c.LampAlpha = 1.0f;
                c.GlowAlpha = 0.30f;
                c.SmokeAlpha = 0.30f;
            }

            const ImVec4 horizon = SkyAt(f, c, f.MoonX, f.Glow, 1.0f);
            const ImVec4 high = SkyAt(f, c, f.MoonX, f.Min.y + (f.Glow - f.Min.y) * 0.45f, 1.0f);
            c.Haze = Mix(horizon, high, 0.20f);
            const float* depth = c.Light ? kLightDepth : kDarkDepth;
            for (int i = 0; i < 3; ++i)
            {
                c.Ridge[i] = Mix(c.Haze, c.Dark, depth[i]);
                c.Mist[i] = Mix(c.Haze, c.Dark, Max(0.0f, depth[i] - 0.30f));
            }
            c.Hill = Mix(c.Haze, c.Dark, depth[3]);
            c.Wall = Mix(c.Hill, c.Ridge[1], 0.40f);
            c.Roof = Mix(c.Hill, c.Moon, c.Light ? 0.10f : 0.09f);
            c.Water = Mix(c.Mist[2], horizon, 0.5f);
            c.Deep = Mix(high, c.Dark, c.Light ? 0.30f : 0.60f);
            c.Mare = Mix(c.Moon, c.Haze, 0.55f);
            c.Cloud = Mix(high, c.Moon, c.Light ? 0.65f : 0.10f);
            c.Smoke = c.Light ? Mix(c.Haze, c.Dark, 0.45f) : Mix(c.Haze, c.Moon, 0.35f);
            return c;
        }

        struct UnitCircle
        {
            static constexpr int kSegments = 40;
            float Cos[kSegments];
            float Sin[kSegments];

            UnitCircle()
            {
                for (int k = 0; k < kSegments; ++k)
                {
                    const float a = kTwoPi * static_cast<float>(k) / static_cast<float>(kSegments);
                    Cos[k] = std::cos(a);
                    Sin[k] = std::sin(a);
                }
            }
        };

        const UnitCircle& Circle()
        {
            static const UnitCircle circle;
            return circle;
        }

        void Glow(ImDrawList* dl, ImVec2 centre, ImVec2 radius, ImVec4 colour, float alpha)
        {
            constexpr int kSegments = UnitCircle::kSegments;
            constexpr int kRings = 4;
            constexpr float kReach[kRings] = { 0.22f, 0.46f, 0.72f, 1.0f };
            constexpr float kFall[kRings] = { 0.82f, 0.44f, 0.12f, 0.0f };
            if (alpha <= 0.002f)
                return;
            const UnitCircle& circle = Circle();
            const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
            dl->PrimReserve(kSegments * 3 + (kRings - 1) * kSegments * 6, 1 + kRings * kSegments);
            const unsigned int base = dl->_VtxCurrentIdx;
            dl->PrimWriteVtx(centre, uv, Tint(colour, alpha));
            for (int ring = 0; ring < kRings; ++ring)
            {
                const ImU32 col = Tint(colour, alpha * kFall[ring]);
                for (int k = 0; k < kSegments; ++k)
                    dl->PrimWriteVtx(ImVec2(centre.x + circle.Cos[k] * radius.x * kReach[ring],
                                            centre.y + circle.Sin[k] * radius.y * kReach[ring]),
                                     uv, col);
            }
            for (int k = 0; k < kSegments; ++k)
            {
                dl->PrimWriteIdx(Index(base, 0));
                dl->PrimWriteIdx(Index(base, 1 + k));
                dl->PrimWriteIdx(Index(base, 1 + (k + 1) % kSegments));
            }
            for (int ring = 0; ring < kRings - 1; ++ring)
            {
                for (int k = 0; k < kSegments; ++k)
                {
                    const int a = 1 + ring * kSegments + k;
                    const int b = 1 + ring * kSegments + (k + 1) % kSegments;
                    dl->PrimWriteIdx(Index(base, a));
                    dl->PrimWriteIdx(Index(base, a + kSegments));
                    dl->PrimWriteIdx(Index(base, b + kSegments));
                    dl->PrimWriteIdx(Index(base, a));
                    dl->PrimWriteIdx(Index(base, b + kSegments));
                    dl->PrimWriteIdx(Index(base, b));
                }
            }
        }

        void Dot(ImDrawList* dl, ImVec2 p, float radius, ImU32 colour)
        {
            constexpr int kSides = 6;
            const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
            const ImU32 clear = colour & ~IM_COL32_A_MASK;
            dl->PrimReserve(kSides * 3, kSides + 1);
            const unsigned int base = dl->_VtxCurrentIdx;
            dl->PrimWriteVtx(p, uv, colour);
            for (int k = 0; k < kSides; ++k)
            {
                const float a = kTwoPi * static_cast<float>(k) / static_cast<float>(kSides);
                dl->PrimWriteVtx(ImVec2(p.x + std::cos(a) * radius, p.y + std::sin(a) * radius), uv, clear);
            }
            for (int k = 0; k < kSides; ++k)
            {
                dl->PrimWriteIdx(Index(base, 0));
                dl->PrimWriteIdx(Index(base, 1 + k));
                dl->PrimWriteIdx(Index(base, 1 + (k + 1) % kSides));
            }
        }

        void Streak(ImDrawList* dl, ImVec2 centre, float half, float thick, ImVec4 colour, float alpha)
        {
            constexpr int kColumns = 5;
            constexpr float kAt[kColumns] = { -1.0f, -0.45f, 0.0f, 0.45f, 1.0f };
            constexpr float kFade[kColumns] = { 0.0f, 0.70f, 1.0f, 0.70f, 0.0f };
            const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
            dl->PrimReserve((kColumns - 1) * 2 * 6, kColumns * 3);
            const unsigned int base = dl->_VtxCurrentIdx;
            for (int col = 0; col < kColumns; ++col)
            {
                const float x = centre.x + kAt[col] * half;
                const ImU32 clear = Tint(colour, 0.0f);
                dl->PrimWriteVtx(ImVec2(x, centre.y - thick), uv, clear);
                dl->PrimWriteVtx(ImVec2(x, centre.y), uv, Tint(colour, alpha * kFade[col]));
                dl->PrimWriteVtx(ImVec2(x, centre.y + thick), uv, clear);
            }
            StripIndices(dl, base, kColumns - 1, 3);
        }

        void DrawSky(ImDrawList* dl, const Frame& f, const Tones& c)
        {
            const float bottom = f.Water + f.U(4.0f);
            const float height = bottom - f.Min.y;
            const int columns = Clamp(static_cast<int>(f.Width() / Px(32.0f)), 16, 96);
            constexpr int kRows = 32;
            const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
            const float fullHeight = f.Max.y - f.Min.y;

            dl->PrimReserve(columns * kRows * 6, (columns + 1) * (kRows + 1));
            const unsigned int base = dl->_VtxCurrentIdx;
            for (int col = 0; col <= columns; ++col)
            {
                const float u = static_cast<float>(col) / static_cast<float>(columns);
                const float x = f.ColumnX(col, columns);
                const float dx = (u - 0.5f) * 2.0f;
                for (int row = 0; row <= kRows; ++row)
                {
                    const float y = f.Min.y + height * static_cast<float>(row) / static_cast<float>(kRows);
                    const float dy = ((y - f.Min.y) / fullHeight - 0.50f) * 2.0f;
                    const float dim = Clamp(1.0f - 0.28f * dx * dx - 0.10f * dy * dy, 0.60f, 1.0f);
                    dl->PrimWriteVtx(ImVec2(x, y), uv, Tint(SkyAt(f, c, x, y, dim), 1.0f));
                }
            }
            StripIndices(dl, base, columns, kRows + 1);
        }

        void DrawStars(ImDrawList* dl, const Frame& f, const Tones& c)
        {
            if (c.StarAlpha <= 0.0f)
                return;
            constexpr int kStars = 110;
            const float lowest = f.Up(215.0f);
            const float fadeFrom = f.Up(330.0f);
            for (int i = 0; i < kStars; ++i)
            {
                const std::uint32_t seed = static_cast<std::uint32_t>(i) * 8u;
                const float x = f.Min.x + f.Width() * Hash(seed + 1u);
                const float y = f.Min.y + (lowest - f.Min.y) * std::pow(Hash(seed + 2u), 1.35f);
                const float bright = Hash(seed + 3u);
                const float dx = (x - f.MoonX) / f.Unit;
                const float dy = (y - f.MoonY) / f.Unit;
                const float clear = Ease(48.0f, 150.0f, std::sqrt(dx * dx + dy * dy));
                const float high = 1.0f - Ease(fadeFrom, lowest, y);
                const float twinkle =
                    0.70f + 0.30f * Wave(f.Time, 0.05 + 0.11 * static_cast<double>(Hash(seed + 4u)),
                                         kTwoPi * Hash(seed + 5u));
                const float alpha = c.StarAlpha * (0.20f + 0.80f * bright * bright) * twinkle * clear * high;
                if (alpha < 0.02f)
                    continue;
                const ImVec4 tone = Mix(c.Sheen, c.Moon, Hash(seed + 6u));
                Dot(dl, ImVec2(x, y), Px(1.1f) + Px(1.1f) * bright * bright * bright, Tint(tone, alpha));
            }
        }

        void DrawMoon(ImDrawList* dl, const Frame& f, const Tones& c)
        {
            const ImVec2 centre(f.MoonX, f.MoonY);
            const float r = f.U(20.0f);
            Glow(dl, centre, ImVec2(r * 11.0f, r * 11.0f), c.Halo, c.HaloAlpha * 0.45f);
            Glow(dl, centre, ImVec2(r * 3.8f, r * 3.8f), c.Moon, c.HaloAlpha);
            dl->AddCircleFilled(centre, r, Tint(c.Moon, 1.0f), 0);
            const float mare = c.Light ? 0.12f : 0.30f;
            Glow(dl, ImVec2(centre.x - r * 0.30f, centre.y - r * 0.22f), ImVec2(r * 0.55f, r * 0.45f), c.Mare, mare);
            Glow(dl, ImVec2(centre.x + r * 0.28f, centre.y + r * 0.10f), ImVec2(r * 0.45f, r * 0.50f), c.Mare, mare);
            Glow(dl, ImVec2(centre.x - r * 0.10f, centre.y + r * 0.45f), ImVec2(r * 0.30f, r * 0.25f), c.Mare, mare);
        }

        struct Cloud
        {
            float Height;
            float Thick;
            float Strength;
            float Waves;
            float Speed;
            float Seed;
        };

        constexpr Cloud kClouds[] = {
            { -118.0f, 18.0f, 0.55f, 1.4f, 0.0036f, 0.3f },
            { 34.0f, 22.0f, 0.75f, 1.0f, 0.0024f, 2.1f },
        };

        void DrawCloud(ImDrawList* dl, const Frame& f, const Tones& c, const Cloud& k)
        {
            constexpr int kPerColumn = 4;
            const int columns = f.Columns(7.0f);
            const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
            const float y0 = f.MoonY + f.U(k.Height);

            dl->PrimReserve(columns * (kPerColumn - 1) * 6, (columns + 1) * kPerColumn);
            const unsigned int base = dl->_VtxCurrentIdx;
            for (int col = 0; col <= columns; ++col)
            {
                const float x = f.ColumnX(col, columns);
                const float s = kTwoPi * k.Waves * f.Along(x);
                const float n = 0.5f + 0.5f * (0.55f * std::sin(s - Phase(f.Time, k.Speed, k.Seed)) +
                                               0.30f * std::sin(2.3f * s - Phase(f.Time, k.Speed * 1.9, k.Seed * 2.7f)) +
                                               0.15f * std::sin(5.1f * s + Phase(f.Time, k.Speed * 0.7, k.Seed * 4.1f)));
                const float density = Ease(0.38f, 0.86f, n);
                const float lumps = 0.5f + 0.5f * std::sin(4.3f * s - Phase(f.Time, k.Speed * 4.3, k.Seed * 1.3f));
                const float y = y0 + f.U(4.0f) * std::sin(0.6f * s + k.Seed);
                const float thick = f.U(k.Thick) * density;
                const float top = y - thick * (0.55f + 0.45f * lumps);
                const float lit = Bell((x - f.MoonX) / f.U(170.0f)) * Bell((y - f.MoonY) / f.U(130.0f));
                const ImVec4 tone = Mix(c.Cloud, c.Moon, c.CloudLit * lit);
                const float alpha = c.CloudAlpha * k.Strength * density * (1.0f + 0.8f * lit);
                const ImU32 clear = Tint(tone, 0.0f);
                dl->PrimWriteVtx(ImVec2(x, top - f.U(3.0f)), uv, clear);
                dl->PrimWriteVtx(ImVec2(x, top + thick * 0.20f), uv, Tint(tone, alpha * 0.80f));
                dl->PrimWriteVtx(ImVec2(x, y), uv, Tint(tone, alpha));
                dl->PrimWriteVtx(ImVec2(x, y + thick * 0.35f), uv, clear);
            }
            StripIndices(dl, base, columns, kPerColumn);
        }

        struct Ridge
        {
            float Base;
            float Rise;
            float Valley;
            float Waves[3];
            float Weights[3];
            float Seed;
            float Trees;
            float Foot;
            float Fade;
            float Spacing;
        };

        constexpr Ridge kRidges[] = {
            { 172.0f, 120.0f, 78.0f, { 2.2f, 5.3f, 12.7f }, { 0.55f, 0.30f, 0.15f }, 0.37f, 0.0f, 120.0f, 64.0f, 4.0f },
            { 124.0f, 80.0f, 58.0f, { 1.7f, 4.1f, 9.3f }, { 0.60f, 0.28f, 0.12f }, 0.81f, 0.0f, 98.0f, 46.0f, 4.0f },
            { 96.0f, 24.0f, 30.0f, { 1.3f, 3.7f, 8.9f }, { 0.55f, 0.30f, 0.15f }, 0.15f, 7.0f, 84.0f, 26.0f, 2.5f },
        };
        constexpr int kRidgeCount = static_cast<int>(sizeof(kRidges) / sizeof(kRidges[0]));

        float RidgeTop(const Frame& f, const Ridge& r, float x)
        {
            const float s = f.Along(x);
            float h = 0.0f;
            for (int i = 0; i < 3; ++i)
                h += r.Weights[i] * Peak(s * r.Waves[i] + r.Seed * static_cast<float>(i + 1));
            float design = r.Base + r.Rise * (h - 0.5f) - r.Valley * Bell((x - f.MoonX) / f.U(210.0f));
            if (r.Trees > 0.0f)
            {
                const float crown = Max(Peak(s * 83.0f + r.Seed), Peak(s * 127.0f + 2.0f * r.Seed));
                design += r.Trees * crown * crown * crown * Ease(0.25f, 0.6f, Peak(s * 6.0f + r.Seed));
            }
            return f.Up(design);
        }

        struct Outline
        {
            int Columns = 0;
            float Top[kMaxColumns + 1];
        };

        void Trace(const Frame& f, const Ridge& r, Outline& out)
        {
            out.Columns = f.Columns(r.Spacing);
            for (int col = 0; col <= out.Columns; ++col)
                out.Top[col] = RidgeTop(f, r, f.ColumnX(col, out.Columns));
        }

        void DrawRidge(ImDrawList* dl, const Frame& f, const Outline& o, const Ridge& r, ImVec4 colour, ImVec4 mist)
        {
            constexpr int kPerColumn = 5;
            const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
            const float foot = f.Up(r.Foot);
            const float from = foot - f.U(r.Fade);
            const float bottom = Max(foot, f.Water + f.U(2.0f));
            const auto tone = [&](float y) { return Mix(colour, mist, Ease(from, foot, y)); };

            dl->PrimReserve(o.Columns * (kPerColumn - 1) * 6, (o.Columns + 1) * kPerColumn);
            const unsigned int base = dl->_VtxCurrentIdx;
            for (int col = 0; col <= o.Columns; ++col)
            {
                const float x = f.ColumnX(col, o.Columns);
                const float top = o.Top[col];
                const ImVec4 edge = tone(top);
                dl->PrimWriteVtx(ImVec2(x, top - 1.0f), uv, Tint(edge, 0.0f));
                dl->PrimWriteVtx(ImVec2(x, top), uv, Tint(edge, 1.0f));
                const float y1 = Max(top, from);
                const float y2 = Max(top, foot);
                dl->PrimWriteVtx(ImVec2(x, y1), uv, Tint(tone(y1), 1.0f));
                dl->PrimWriteVtx(ImVec2(x, y2), uv, Tint(tone(y2), 1.0f));
                dl->PrimWriteVtx(ImVec2(x, Max(top, bottom)), uv, Tint(mist, 1.0f));
            }
            StripIndices(dl, base, o.Columns, kPerColumn);
        }

        void DrawReflection(ImDrawList* dl, const Frame& f, const Outline& o, ImVec4 colour, ImVec4 mist, float alpha)
        {
            constexpr int kPerColumn = 4;
            const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
            dl->PrimReserve(o.Columns * (kPerColumn - 1) * 6, (o.Columns + 1) * kPerColumn);
            const unsigned int base = dl->_VtxCurrentIdx;
            for (int col = 0; col <= o.Columns; ++col)
            {
                const float x = f.ColumnX(col, o.Columns);
                const float depth = Max(0.0f, f.Water - o.Top[col]) * 0.85f;
                dl->PrimWriteVtx(ImVec2(x, f.Water), uv, Tint(mist, alpha));
                dl->PrimWriteVtx(ImVec2(x, f.Water + depth * 0.45f), uv, Tint(colour, alpha));
                dl->PrimWriteVtx(ImVec2(x, f.Water + depth), uv, Tint(colour, alpha));
                dl->PrimWriteVtx(ImVec2(x, f.Water + depth + 1.0f), uv, Tint(colour, 0.0f));
            }
            StripIndices(dl, base, o.Columns, kPerColumn);
        }

        struct Drift
        {
            float Height;
            float Thick;
            float Strength;
            float Waves;
            float Speed;
            float Seed;
        };

        constexpr Drift kDrifts[] = {
            { 110.0f, 20.0f, 0.75f, 2.3f, 0.010f, 0.6f },
            { 84.0f, 4.0f, 1.2f, 3.1f, -0.012f, 1.9f },
        };

        void DrawDrift(ImDrawList* dl, const Frame& f, const Drift& d, ImVec4 colour, float strength)
        {
            constexpr int kPerColumn = 4;
            const int columns = f.Columns(8.0f);
            const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
            const float y = f.Up(d.Height);
            const float thick = f.U(d.Thick);
            const ImU32 clear = Tint(colour, 0.0f);

            dl->PrimReserve(columns * (kPerColumn - 1) * 6, (columns + 1) * kPerColumn);
            const unsigned int base = dl->_VtxCurrentIdx;
            for (int col = 0; col <= columns; ++col)
            {
                const float x = f.ColumnX(col, columns);
                const float s = kTwoPi * d.Waves * f.Along(x);
                const float n = 0.5f + 0.5f * (0.6f * std::sin(s - Phase(f.Time, d.Speed, d.Seed)) +
                                               0.4f * std::sin(2.7f * s - Phase(f.Time, d.Speed * 2.2, d.Seed * 3.1f)));
                const float alpha = strength * d.Strength * (0.30f + 0.70f * n);
                dl->PrimWriteVtx(ImVec2(x, y - thick), uv, clear);
                dl->PrimWriteVtx(ImVec2(x, y - thick * 0.25f), uv, Tint(colour, alpha));
                dl->PrimWriteVtx(ImVec2(x, y + thick * 0.25f), uv, Tint(colour, alpha));
                dl->PrimWriteVtx(ImVec2(x, y + thick), uv, clear);
            }
            StripIndices(dl, base, columns, kPerColumn);
        }

        void DrawMoonPath(ImDrawList* dl, const Frame& f, const Tones& c)
        {
            constexpr int kStreaks = 26;
            const float top = f.Water + f.U(2.0f);
            const float bottom = f.Up(14.0f);
            if (bottom <= top)
                return;
            Glow(dl, ImVec2(f.MoonX, (top + bottom) * 0.5f), ImVec2(f.U(40.0f), (bottom - top) * 0.80f), c.Moon,
                 c.PathAlpha * 0.14f);
            for (int j = 0; j < kStreaks; ++j)
            {
                const std::uint32_t seed = 900u + static_cast<std::uint32_t>(j) * 8u;
                const float s = (static_cast<float>(j) + 0.2f + 0.6f * Hash(seed)) / static_cast<float>(kStreaks);
                const float y = top + (bottom - top) * std::pow(s, 1.3f);
                const float offset = f.U(3.0f + 12.0f * s) * (2.0f * Hash(seed + 1u) - 1.0f);
                const float sway = f.U(1.5f + 3.0f * s) *
                                   Wave(f.Time, 0.05 + 0.05 * static_cast<double>(Hash(seed + 2u)), kTwoPi * Hash(seed + 3u));
                const float half = f.U(4.0f + 20.0f * s) * (0.35f + 0.65f * Hash(seed + 4u)) *
                                   (0.75f + 0.25f * Wave(f.Time, 0.08 + 0.06 * static_cast<double>(Hash(seed + 5u)),
                                                         kTwoPi * Hash(seed + 6u)));
                const float shimmer =
                    0.5f + 0.5f * Wave(f.Time, 0.12 + 0.12 * static_cast<double>(Hash(seed + 7u)), static_cast<float>(j) * 2.3f);
                const float alpha = c.PathAlpha * (0.20f + 0.80f * shimmer * shimmer) * (1.0f - 0.30f * s);
                Streak(dl, ImVec2(f.MoonX + offset + sway, y), half, f.U(0.6f + 0.8f * s), c.Moon, alpha);
            }
        }

        void DrawFarLight(ImDrawList* dl, const Frame& f, const Tones& c)
        {
            const ImVec2 at(f.MoonX - f.U(232.0f), f.Water - f.U(2.5f));
            const float flicker = 0.88f + 0.12f * Wave(f.Time, 0.13, 2.0f);
            Glow(dl, at, ImVec2(f.U(16.0f), f.U(10.0f)), c.Ember, c.GlowAlpha * 0.8f * flicker);
            dl->AddRectFilled(ImVec2(at.x - f.U(1.4f), at.y - f.U(1.3f)), ImVec2(at.x + f.U(1.4f), at.y + f.U(1.0f)),
                              Tint(c.Lamp, c.LampAlpha * flicker));
            for (int j = 0; j < 4; ++j)
            {
                const float k = static_cast<float>(j);
                const float shimmer = 0.6f + 0.4f * Wave(f.Time, 0.17 + 0.05 * static_cast<double>(j), k * 1.9f);
                Streak(dl, ImVec2(at.x + f.U(0.8f) * Wave(f.Time, 0.07, k), f.Water + f.U(3.5f + 4.5f * k)),
                       f.U(3.2f - 0.4f * k), f.U(0.7f), c.Ember, c.LampAlpha * 0.40f * (1.0f - 0.2f * k) * shimmer);
            }
        }

        float GroundDesign(const Frame& f, float x)
        {
            const float centre = (f.Min.x + f.Max.x) * 0.5f;
            const float left = 16.0f + 96.0f * Ease(centre - Px(150.0f), f.Min.x + f.U(30.0f), x);
            const float right = 16.0f + 46.0f * Ease(f.CabinX - f.U(150.0f), f.CabinX - f.U(40.0f), x) +
                                44.0f * Ease(f.CabinX + f.U(70.0f), f.Max.x - f.U(30.0f), x);
            const float s = f.Along(x);
            const float bumps = 4.0f * std::sin(kTwoPi * 3.1f * s + 0.7f) + 2.5f * std::sin(kTwoPi * 7.3f * s + 2.1f);
            return Max(left, right) + bumps;
        }

        float GroundTop(const Frame& f, float x)
        {
            const float settle = Bell((x - f.CabinX) / f.U(40.0f));
            const float design = GroundDesign(f, x);
            return f.Up(design + (GroundDesign(f, f.CabinX) - design) * settle * settle);
        }

        void DrawGround(ImDrawList* dl, const Frame& f, ImVec4 colour)
        {
            constexpr int kPerColumn = 3;
            const int columns = f.Columns(4.0f);
            const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
            const ImU32 solid = Tint(colour, 1.0f);
            dl->PrimReserve(columns * (kPerColumn - 1) * 6, (columns + 1) * kPerColumn);
            const unsigned int base = dl->_VtxCurrentIdx;
            for (int col = 0; col <= columns; ++col)
            {
                const float x = f.ColumnX(col, columns);
                const float top = GroundTop(f, x);
                dl->PrimWriteVtx(ImVec2(x, top - 1.0f), uv, Tint(colour, 0.0f));
                dl->PrimWriteVtx(ImVec2(x, top), uv, solid);
                dl->PrimWriteVtx(ImVec2(x, f.Max.y), uv, solid);
            }
            StripIndices(dl, base, columns, kPerColumn);
        }

        struct Pine
        {
            int Anchor;
            float Offset;
            float Height;
        };

        constexpr Pine kPines[] = {
            { -1, 10.0f, 52.0f }, { -1, 30.0f, 66.0f }, { -1, 52.0f, 46.0f }, { -1, 86.0f, 56.0f },
            { -1, 122.0f, 38.0f }, { -1, 160.0f, 28.0f },
            { 0, -54.0f, 34.0f }, { 0, 34.0f, 54.0f }, { 0, 52.0f, 40.0f },
            { 1, -12.0f, 60.0f }, { 1, -38.0f, 46.0f }, { 1, -66.0f, 64.0f }, { 1, -100.0f, 42.0f },
        };

        void DrawPine(ImDrawList* dl, const Frame& f, float x, float height, ImU32 colour)
        {
            const float ground = GroundTop(f, x) + f.U(3.0f);
            const float h = f.U(height);
            const float w = h * 0.42f;
            dl->AddRectFilled(ImVec2(x - h * 0.03f, ground - h * 0.18f), ImVec2(x + h * 0.03f, ground), colour);
            constexpr float kFoot[3] = { 0.12f, 0.38f, 0.62f };
            constexpr float kTip[3] = { 0.62f, 0.84f, 1.0f };
            constexpr float kSpread[3] = { 0.50f, 0.38f, 0.26f };
            for (int i = 0; i < 3; ++i)
            {
                const float foot = ground - h * kFoot[i];
                dl->AddTriangleFilled(ImVec2(x, ground - h * kTip[i]), ImVec2(x + w * kSpread[i], foot),
                                      ImVec2(x - w * kSpread[i], foot), colour);
            }
        }

        void DrawPines(ImDrawList* dl, const Frame& f, ImVec4 colour)
        {
            const ImU32 col = Tint(colour, 1.0f);
            for (const Pine& p : kPines)
            {
                const float anchor = p.Anchor < 0 ? f.Min.x : (p.Anchor > 0 ? f.Max.x : f.CabinX);
                DrawPine(dl, f, anchor + f.U(p.Offset), p.Height, col);
            }
        }

        struct Sketch
        {
            ImDrawList* List;
            float Unit;
            ImVec2 At;

            ImVec2 P(float x, float y) const { return ImVec2(At.x + x * Unit, At.y + y * Unit); }

            void Box(float x0, float y0, float x1, float y1, ImU32 colour) const
            {
                List->AddRectFilled(P(x0, y0), P(x1, y1), colour);
            }

            void Tri(float x0, float y0, float x1, float y1, float x2, float y2, ImU32 colour) const
            {
                List->AddTriangleFilled(P(x0, y0), P(x1, y1), P(x2, y2), colour);
            }
        };

        void DrawSmoke(ImDrawList* dl, const Frame& f, const Tones& c, ImVec2 from)
        {
            constexpr int kRows = 28;
            const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
            const float rise = f.U(84.0f);
            dl->PrimReserve(kRows * 2 * 6, (kRows + 1) * 3);
            const unsigned int base = dl->_VtxCurrentIdx;
            for (int i = 0; i <= kRows; ++i)
            {
                const float s = static_cast<float>(i) / static_cast<float>(kRows);
                const float drift = -f.U(26.0f) * std::pow(s, 1.7f);
                const float curl = f.U(1.5f + 6.0f * s) * std::sin(7.0f * s - Phase(f.Time, 0.10));
                const float x = from.x + drift + curl;
                const float y = from.y - rise * s;
                const float half = f.U(1.6f + 9.0f * s);
                const float puffs = 0.72f + 0.28f * std::sin(11.0f * s - Phase(f.Time, 0.16, 1.0f));
                const float alpha = c.SmokeAlpha * Ease(0.0f, 0.06f, s) * std::pow(1.0f - s, 1.5f) * puffs;
                const ImU32 clear = Tint(c.Smoke, 0.0f);
                dl->PrimWriteVtx(ImVec2(x - half, y), uv, clear);
                dl->PrimWriteVtx(ImVec2(x, y), uv, Tint(c.Smoke, alpha));
                dl->PrimWriteVtx(ImVec2(x + half, y), uv, clear);
            }
            StripIndices(dl, base, kRows, 3);
        }

        void DrawCabin(ImDrawList* dl, const Frame& f, const Tones& c)
        {
            const Sketch k{ dl, f.Unit * kCabinScale, ImVec2(f.CabinX, GroundTop(f, f.CabinX) + f.U(1.0f)) };
            const ImU32 wall = Tint(c.Wall, 1.0f);
            const ImU32 roof = Tint(c.Roof, 1.0f);

            k.Box(4.5f, -30.0f, 8.0f, -19.0f, wall);
            k.Box(4.0f, -31.0f, 8.5f, -29.6f, wall);
            k.Box(-24.0f, -10.0f, -9.0f, 0.0f, wall);
            k.Tri(-10.0f, -18.0f, -10.0f, -9.5f, -26.0f, -9.5f, roof);
            k.Box(-10.0f, -15.0f, 10.0f, 0.0f, wall);
            k.Tri(0.0f, -28.0f, 13.0f, -14.0f, -13.0f, -14.0f, roof);
            k.Tri(0.0f, -24.5f, 9.6f, -14.6f, -9.6f, -14.6f, wall);

            const float flicker = 0.92f + 0.05f * Wave(f.Time, 0.21) + 0.03f * Wave(f.Time, 0.47, 1.3f);
            const ImU32 lamp = Tint(c.Lamp, c.LampAlpha * flicker);
            const ImU32 ember = Tint(c.Ember, c.LampAlpha * flicker);
            dl->AddRectFilledMultiColor(k.P(-4.5f, -11.5f), k.P(4.5f, -4.0f), lamp, lamp, ember, ember);
            dl->AddRectFilledMultiColor(k.P(-20.0f, -7.5f), k.P(-14.0f, -3.0f), lamp, lamp, ember, ember);
            k.Box(-0.5f, -11.5f, 0.5f, -4.0f, wall);
            k.Box(-4.5f, -8.2f, 4.5f, -7.2f, wall);
            dl->AddCircleFilled(k.P(0.0f, -19.0f), k.Unit * 1.7f, ember, 0);

            Glow(dl, k.P(-4.0f, -8.0f), ImVec2(k.Unit * 46.0f, k.Unit * 34.0f), c.Ember, c.GlowAlpha * 0.55f * flicker);
            Glow(dl, k.P(0.0f, -8.0f), ImVec2(k.Unit * 20.0f, k.Unit * 16.0f), c.Lamp, c.GlowAlpha * 0.8f * flicker);
            Glow(dl, k.P(-6.0f, 3.0f), ImVec2(k.Unit * 48.0f, k.Unit * 7.0f), c.Ember, c.GlowAlpha * 0.5f * flicker);

            DrawSmoke(dl, f, c, k.P(6.25f, -31.0f));
        }
    }

    void DrawLockScene(ImDrawList* dl, ImVec2 min, ImVec2 max, double seconds)
    {
        const float width = max.x - min.x;
        const float height = max.y - min.y;
        if (width <= 1.0f || height <= 1.0f)
            return;

        Frame f;
        f.Min = min;
        f.Max = max;
        f.Time = seconds;
        f.Unit = Clamp(height * 0.40f, Px(200.0f), Px(500.0f)) / kReferenceLand;
        const float centre = (min.x + max.x) * 0.5f;
        f.MoonX = centre - Px(41.0f);
        f.MoonY = min.y + height * 0.29f;
        f.Glow = f.Up(150.0f);
        f.Water = f.Up(84.0f);
        f.CabinX = centre + Px(200.0f);

        const Tones c = MakeTones(f);

        DrawSky(dl, f, c);
        DrawStars(dl, f, c);
        DrawMoon(dl, f, c);
        for (const Cloud& cloud : kClouds)
            DrawCloud(dl, f, c, cloud);

        Outline outlines[kRidgeCount];
        for (int i = 0; i < kRidgeCount; ++i)
            Trace(f, kRidges[i], outlines[i]);

        DrawRidge(dl, f, outlines[0], kRidges[0], c.Ridge[0], c.Mist[0]);
        DrawRidge(dl, f, outlines[1], kRidges[1], c.Ridge[1], c.Mist[1]);
        DrawDrift(dl, f, kDrifts[0], c.Mist[1], c.MistAlpha);
        DrawRidge(dl, f, outlines[2], kRidges[2], c.Ridge[2], c.Mist[2]);

        dl->AddRectFilledMultiColor(ImVec2(min.x, f.Water), max, Tint(c.Water, 1.0f), Tint(c.Water, 1.0f),
                                    Tint(c.Deep, 1.0f), Tint(c.Deep, 1.0f));
        DrawReflection(dl, f, outlines[1], Mix(c.Ridge[1], c.Water, 0.35f), Mix(c.Mist[1], c.Water, 0.35f), 0.85f);
        DrawReflection(dl, f, outlines[2], Mix(c.Ridge[2], c.Water, 0.25f), Mix(c.Mist[2], c.Water, 0.25f), 0.90f);
        DrawMoonPath(dl, f, c);
        DrawDrift(dl, f, kDrifts[1], c.Mist[0], c.MistAlpha);
        DrawFarLight(dl, f, c);

        DrawGround(dl, f, c.Hill);
        DrawPines(dl, f, c.Hill);
        DrawCabin(dl, f, c);
    }
}
