"""
Structural hover test: validate that all themes produce visible hover colors
and that the QProxyStyle code handles the required control elements.

No pixel sampling — checks theme JSON values and source code patterns.
"""
import json
import os
import re
import sys


def hex_to_rgb(h):
    h = h.lstrip('#')
    return tuple(int(h[i:i+2], 16) for i in (0, 2, 4))


def color_dist(c1, c2):
    return sum(abs(a - b) for a, b in zip(c1, c2))


def lighter_130(rgb):
    """Approximate Qt's QColor::lighter(130) for dark grays."""
    r, g, b = rgb
    return (min(255, int(r * 1.3) + 1),
            min(255, int(g * 1.3) + 1),
            min(255, int(b * 1.3) + 1))


def load_themes():
    themes = {}
    theme_dir = os.path.join(os.path.dirname(__file__),
                             '..', 'src', 'themes', 'defaults')
    if not os.path.isdir(theme_dir):
        return themes
    for name in os.listdir(theme_dir):
        if name.endswith('.json'):
            with open(os.path.join(theme_dir, name)) as f:
                themes[name] = json.load(f)
    return themes


def test_hover_visibility(themes):
    """Every theme must have hover visually distinct from background.
    If raw values are identical, Theme::fromJson applies lighter(130)."""
    ok = True
    for name, data in sorted(themes.items()):
        bg = hex_to_rgb(data['background'])
        hover = hex_to_rgb(data['hover'])
        dist = color_dist(bg, hover)

        if dist < 20:
            # fromJson will fix this — verify the fix produces sufficient contrast
            fixed = lighter_130(bg)
            fixed_dist = color_dist(bg, fixed)
            if fixed_dist < 15:
                print(f"  FAIL: {name}: hover==bg and lighter(130) still too close "
                      f"(dist={fixed_dist})")
                ok = False
            else:
                print(f"  OK:   {name}: hover==bg, fromJson fixup -> "
                      f"dist {dist}->{fixed_dist}")
        else:
            print(f"  OK:   {name}: hover distinct (dist={dist})")
    return ok


def test_proxystyle_handlers():
    """Verify MenuBarStyle handles CE_MenuBarItem, CE_MenuItem, CE_MenuBarEmptyArea."""
    src = os.path.join(os.path.dirname(__file__), '..', 'src', 'main.cpp')
    with open(src) as f:
        code = f.read()

    required = {
        'CE_MenuBarItem':      r'element\s*==\s*CE_MenuBarItem',
        'CE_MenuItem':         r'element\s*==\s*CE_MenuItem',
        'CE_MenuBarEmptyArea': r'element\s*==\s*CE_MenuBarEmptyArea',
        'State_Selected':      r'State_Selected',
        'QPalette::Mid':       r'QPalette::Mid',
    }

    ok = True
    for label, pattern in required.items():
        if re.search(pattern, code):
            print(f"  OK:   MenuBarStyle handles {label}")
        else:
            print(f"  FAIL: MenuBarStyle missing {label}")
            ok = False
    return ok


def test_no_menubar_css():
    """Verify no CSS stylesheet is set on QMenuBar (would bypass QProxyStyle)."""
    src_dir = os.path.join(os.path.dirname(__file__), '..', 'src')
    ok = True
    for root, _, files in os.walk(src_dir):
        for fname in files:
            if not fname.endswith('.cpp'):
                continue
            path = os.path.join(root, fname)
            with open(path, encoding='utf-8', errors='replace') as f:
                for i, line in enumerate(f, 1):
                    # Check for menuBar/m_menuBar stylesheet calls
                    if ('menuBar' in line or 'm_menuBar' in line) and \
                       'setStyleSheet' in line:
                        print(f"  FAIL: CSS on QMenuBar at {fname}:{i}: "
                              f"{line.strip()}")
                        ok = False
    if ok:
        print("  OK:   No CSS on QMenuBar")
    return ok


def test_hover_fixup_in_fromjson():
    """Verify Theme::fromJson applies the hover fixup."""
    src = os.path.join(os.path.dirname(__file__),
                       '..', 'src', 'themes', 'theme.cpp')
    with open(src) as f:
        code = f.read()

    if 'lighter(130)' in code and 't.hover' in code:
        print("  OK:   Theme::fromJson has hover fixup")
        return True
    else:
        print("  FAIL: Theme::fromJson missing hover fixup")
        return False


def main():
    themes = load_themes()
    if not themes:
        print("FAIL: No theme files found")
        return 1

    all_ok = True

    print("--- Test 1: Hover visibility across themes ---")
    all_ok &= test_hover_visibility(themes)

    print("\n--- Test 2: QProxyStyle handles required elements ---")
    all_ok &= test_proxystyle_handlers()

    print("\n--- Test 3: No CSS on QMenuBar ---")
    all_ok &= test_no_menubar_css()

    print("\n--- Test 4: Theme::fromJson hover fixup ---")
    all_ok &= test_hover_fixup_in_fromjson()

    print(f"\n{'='*50}")
    if all_ok:
        print("ALL HOVER TESTS PASSED")
        return 0
    else:
        print("SOME HOVER TESTS FAILED")
        return 1


if __name__ == '__main__':
    sys.exit(main())
