#!/usr/bin/env python3
"""Lay du lieu thoi that tu Open-Meteo (khong can API key)."""
import json
import os
import platform
import urllib.request

URL = ("https://api.open-meteo.com/v1/forecast"
       "?latitude=21.03&longitude=105.85"
       "&current=temperature_2m,relative_humidity_2m,weather_code"
       "&timezone=Asia%2FBangkok")

with urllib.request.urlopen(URL, timeout=20) as resp:
    data = json.load(resp)
cur = data["current"]

print(json.dumps({
    "program": "weather.py",
    "source": "Open-Meteo API",
    "city": "HaNoi",
    "temperature_2m_C": cur["temperature_2m"],
    "relative_humidity_2m_pct": cur["relative_humidity_2m"],
    "weather_code": cur["weather_code"],
    "observed_at": cur["time"],
    "ran_on": {
        "arch": platform.machine(),
        "kernel": open("/proc/sys/kernel/osrelease").read().strip(),
        "hostname": os.uname().nodename,
        "pid": os.getpid(),
    },
}, ensure_ascii=False, sort_keys=True))
