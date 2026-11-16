# nginx recipe

Static site served by nginx:alpine, built with `minicontainer build` and published on port 8080.

```bash
minicontainer pull nginx:alpine
minicontainer build -t recipe-nginx:1 recipes/nginx
minicontainer run -d --name recipe-nginx --image=recipe-nginx:1 --ram=64M --net=bridge \
  -p 8080:8080 nginx -g 'daemon off;'
curl -fsS http://127.0.0.1:8080/ | grep -o 'nginx recipe is running'
minicontainer logs recipe-nginx
minicontainer stop recipe-nginx && minicontainer rm recipe-nginx
```

Requires root, cgroup v2, `ip`/`iptables` for the bridge, and network access for the base image.
Expected output is in `expected.txt`. If port 8080 is busy, publish another host port
(`-p 8081:8080`). nginx listens on **8080 inside** because ports below 1024 are not bindable in the
container (see COMPAT.md).
