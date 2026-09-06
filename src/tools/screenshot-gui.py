#!/usr/bin/env python3
"""
screenshot-gui.py -- capture each page of the formuls control GUI as a PNG.

Requires formuls to be running so that Open Stage Control is serving on
the expected port (default 9001).  Uses Playwright/Chromium to load the
GUI in a headless browser, click through each tab, and save a screenshot.

Prerequisites:
    pip3 install playwright
    python3 -m playwright install chromium

Usage:
    python3 src/tools/screenshot-gui.py                   # saves to ~/Desktop/formuls-gui/
    python3 src/tools/screenshot-gui.py -o /tmp/gui       # saves to /tmp/gui/
    python3 src/tools/screenshot-gui.py --port 9002       # if O-S-C runs on a different port
"""

import argparse
import json
import sys
import time
from pathlib import Path

try:
    from playwright.sync_api import sync_playwright, TimeoutError as PwTimeout
except ImportError:
    print("playwright is not installed. Install it with:")
    print("  pip3 install playwright")
    print("  python3 -m playwright install chromium")
    sys.exit(1)


def load_tab_ids(session_path: Path) -> list:
    """Read the tab IDs from the O-S-C session JSON, in order."""
    with open(session_path) as f:
        data = json.load(f)

    tabs = []

    def walk(obj):
        if isinstance(obj, dict):
            if obj.get("type") == "tab":
                tabs.append(obj.get("id", "unknown"))
            for v in obj.values():
                walk(v)
        elif isinstance(obj, list):
            for item in obj:
                walk(item)

    walk(data)
    return tabs


def main():
    parser = argparse.ArgumentParser(
        description="Screenshot each page of the formuls control GUI.")
    parser.add_argument("-o", "--output", default=None,
                        help="Output directory (default: ~/Desktop/formuls-gui/)")
    parser.add_argument("--port", type=int, default=9001,
                        help="Open Stage Control port (default: 9001)")
    parser.add_argument("--width", type=int, default=2048,
                        help="Viewport width in pixels (default: 2048)")
    parser.add_argument("--height", type=int, default=1536,
                        help="Viewport height in pixels (default: 1536)")
    parser.add_argument("--timeout", type=int, default=10000,
                        help="Tab-switch timeout in ms (default: 10000)")
    parser.add_argument("--session", default=None,
                        help="Path to _main.json (auto-detected by default)")
    args = parser.parse_args()

    # Locate the session JSON.
    if args.session:
        session_path = Path(args.session)
    else:
        # Walk up from this script to find src/gui/_main.json.
        here = Path(__file__).resolve().parent
        for candidate in [here / ".." / "gui" / "_main.json",
                          here / ".." / ".." / "gui" / "_main.json",
                          here / "gui" / "_main.json"]:
            candidate = candidate.resolve()
            if candidate.exists():
                session_path = candidate
                break
        else:
            print("Could not find src/gui/_main.json.  Pass --session <path>.")
            sys.exit(1)

    tab_ids = load_tab_ids(session_path)
    if not tab_ids:
        print("No tabs found in " + str(session_path))
        sys.exit(1)

    # Build the list of (tab_id, nav_index) pairs to capture.
    # The JSON tab order matches the O-S-C navigation bar order.
    # skip_tabs: tab IDs to exclude from screenshots.
    skip_tabs = {"RESET"}
    tab_plan = [(tid, i) for i, tid in enumerate(tab_ids) if tid not in skip_tabs]

    # Output directory.
    out_dir = Path(args.output) if args.output else Path.home() / "Desktop" / "formuls-gui"
    out_dir.mkdir(parents=True, exist_ok=True)

    url = f"http://127.0.0.1:{args.port}"
    print(f"Connecting to {url} ...")
    print(f"Session: {session_path}")
    print(f"Tabs to capture: {len(tab_plan)}  {[t[0] for t in tab_plan]}")
    print(f"Viewport: {args.width}x{args.height}")
    print(f"Output:   {out_dir}/\n")

    with sync_playwright() as pw:
        browser = pw.chromium.launch(headless=True)
        context = browser.new_context(
            viewport={"width": args.width, "height": args.height},
            device_scale_factor=1,
        )
        page = context.new_page()

        # Load the GUI.
        try:
            page.goto(url, wait_until="networkidle", timeout=args.timeout)
        except PwTimeout:
            print(f"Could not connect to {url}.  Is formuls running?")
            browser.close()
            sys.exit(1)

        # O-S-C renders tabs as .tablink elements inside a .navigation bar.
        # Wait for the navigation bar to appear and the first tab to be active.
        page.wait_for_selector(".navigation .tablink", timeout=args.timeout)
        time.sleep(2)  # let canvases render

        tab_links = page.query_selector_all(".navigation .tablink")

        saved = []
        skipped = []

        for tab_id, nav_index in tab_plan:
            if nav_index >= len(tab_links):
                print(f"  [{tab_id}] SKIP (nav link {nav_index} not found)")
                continue

            # Skip tabs that are hidden by the O-S-C session (display: none).
            is_visible = tab_links[nav_index].evaluate(
                "el => window.getComputedStyle(el).display !== 'none'")
            if not is_visible:
                skipped.append(tab_id)
                print(f"  [{tab_id}] SKIP (hidden by session)")
                continue

            try:
                tab_links[nav_index].click()

                # Wait for the tab content to settle — canvases need time
                # to render after a tab switch.
                time.sleep(1.5)

                # Take the screenshot.
                filename = f"{tab_id}.png"
                filepath = out_dir / filename
                page.screenshot(path=str(filepath), full_page=False)
                saved.append(filepath)
                print(f"  [{tab_id}] -> {filepath.name}")

            except Exception as e:
                print(f"  [{tab_id}] ERROR: {e}")

        browser.close()

    print(f"\nDone.  {len(saved)} screenshots saved to {out_dir}/"
          + (f"  ({len(skipped)} hidden tabs skipped)" if skipped else ""))


if __name__ == "__main__":
    main()
