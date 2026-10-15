"""
The blended art style: A Link to the Past's dungeon architecture, lit and colored the
way Minish Cap is, with Minish Cap's textured floors in the A Link to the Past colors.
"""
import colorsys

# Minish Cap's darkest outline is a warm brown-black, not A Link to the Past's grey
MC_OUTLINE = (32, 24, 0)


def blend_color(rgb):
    """One A Link to the Past color, re-lit in Minish Cap's style: brighter
    mid-tones, softer saturation, sunlit warm highlights and warm shadows."""
    r, g, b = [c / 255 for c in rgb]
    h, l, s = colorsys.rgb_to_hls(r, g, b)
    if l < 0.12:
        return MC_OUTLINE
    l2 = 0.10 + 0.92 * (l ** 0.82)
    s2 = min(1.0, s * 0.85 + 0.05)
    # pull hue toward warm yellow in the light, leave the shadows' hue alone
    warm = 0.13
    pull = 0.18 * max(0.0, l2 - 0.45) / 0.55
    dh = ((warm - h + 0.5) % 1.0) - 0.5
    h2 = (h + dh * pull) % 1.0
    r2, g2, b2 = colorsys.hls_to_rgb(h2, min(l2, 0.97), s2)
    return tuple(min(248, int(round(c * 255 / 8)) * 8) for c in (r2, g2, b2))


def recolor_toward(rgb, target_rgb, keep_lightness=True):
    """A Minish Cap color moved to another hue and saturation (for floors), keeping its
    lightness so the texture survives."""
    r, g, b = [c / 255 for c in rgb]
    h, l, s = colorsys.rgb_to_hls(r, g, b)
    th, tl, ts = colorsys.rgb_to_hls(*[c / 255 for c in target_rgb])
    l2 = l * 0.6 + tl * 0.4 if not keep_lightness else l
    r2, g2, b2 = colorsys.hls_to_rgb(th, l2, ts * 0.9)
    return tuple(min(248, int(round(c * 255 / 8)) * 8) for c in (r2, g2, b2))
