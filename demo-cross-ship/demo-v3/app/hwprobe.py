#!/usr/bin/env python3
"""Lop HAL: dich thiet that cua moi may thanh cac khai nien logic chung.

Chay trong container co san:
  /hostdev = bind /dev cua may chu (ro, rbind)
  /hostsys = bind /sys cua may chu (ro, rbind)
Khong hard-code ten thiet bi cua rieng may nao o tang ung dung.
"""
import glob
import json
import os
import platform

# Bang dich: ten that theo tung board -> cac kha nang logic (uart/gpio/i2c/thermal)
BACKENDS = {
    "amlogic": {
        "soc": "Amlogic S905W (meson)",
        "uart": ("/hostdev/ttyAML*", "meson_uart"),
        "gpio": ("/hostdev/gpiochip*", "gpiochip"),
        "i2c": ("/hostdev/i2c-*", "i2c-dev"),
    },
    "pc_x86": {
        "soc": "x86_64 PC",
        "uart": ("/hostdev/ttyS*", "8250/16550"),
        "gpio": ("/hostdev/gpiochip*", "gpiochip"),
        "i2c": ("/hostdev/i2c-*", "i2c-dev"),
    },
}


def detect():
    forced = os.environ.get("HW_BACKEND")
    if forced in BACKENDS:
        return forced, "forced"
    if glob.glob("/hostdev/ttyAML*"):
        return "amlogic", "auto:ttyAML"
    if glob.glob("/hostdev/ttyS*"):
        return "pc_x86", "auto:ttyS"
    return "unknown", "none"


def thermal():
    for path in sorted(glob.glob("/hostsys/class/thermal/thermal_zone*/temp")):
        try:
            milli = int(open(path).read().strip())
        except (OSError, ValueError):
            continue
        return {"source": path, "millidegrees": milli, "celsius": round(milli / 1000, 1)}
    return None


def count(pattern):
    hits = sorted(glob.glob(pattern))
    return {"count": len(hits), "sample": [os.path.basename(h) for h in hits[:4]]}


def main():
    backend, how = detect()
    spec = BACKENDS.get(backend)
    caps = {}
    missing = []
    if spec:
        for key in ("uart", "gpio", "i2c"):
            pattern, driver = spec[key]
            item = count(pattern)
            item["driver_hint"] = driver
            caps[key] = item
            if item["count"] == 0:
                missing.append(key)
        caps["spi"] = count("/hostdev/spidev*")
        caps["thermal"] = thermal()
        if caps["thermal"] is None:
            missing.append("thermal")

    status = "OK" if spec and not missing else ("WRONG_BACKEND" if how == "forced" else "PARTIAL")
    print(json.dumps({
        "program": "hwprobe.py (HAL)",
        "backend": backend,
        "selected_by": how,
        "arch": platform.machine(),
        "kernel": open("/proc/sys/kernel/osrelease").read().strip(),
        "pid": os.getpid(),
        "soc_hint": spec["soc"] if spec else None,
        "capabilities": caps,
        "missing": missing,
        "status": status,
    }, sort_keys=True))
    # Dòng doc cho nguoi xem: JSON o tren de script kiem tra, dong nay de hieu nhanh.
    c = caps.get("thermal") or {"celsius": 0.0}
    print("=> backend=%s (%s) | uart=%d | gpio=%d | i2c=%d | %.1fC | missing=%s | %s"
          % (backend, how, caps.get("uart", {}).get("count", 0),
             caps.get("gpio", {}).get("count", 0), caps.get("i2c", {}).get("count", 0),
             c["celsius"], missing, status))


if __name__ == "__main__":
    main()
