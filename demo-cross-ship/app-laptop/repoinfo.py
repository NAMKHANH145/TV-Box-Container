#!/usr/bin/env python3
"""Lay thong tin repo tu GitHub API (khong can API key)."""
import json
import os
import platform
import urllib.request

URL = "https://api.github.com/repos/NAMKHANH145/TV-Box-Container"
req = urllib.request.Request(URL, headers={"User-Agent": "minicontainer-demo"})
with urllib.request.urlopen(req, timeout=20) as resp:
    data = json.load(resp)

print(json.dumps({
    "program": "repoinfo.py",
    "source": "GitHub REST API",
    "repo": data["full_name"],
    "stars": data["stargazers_count"],
    "description": data["description"],
    "default_branch": data["default_branch"],
    "ran_on": {
        "arch": platform.machine(),
        "kernel": open("/proc/sys/kernel/osrelease").read().strip(),
        "hostname": os.uname().nodename,
        "pid": os.getpid(),
    },
}, ensure_ascii=False, sort_keys=True))
