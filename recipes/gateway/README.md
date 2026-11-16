# Gateway recipe (2 services)

nginx in front of a Python API, both on the compose-lite bridge network. nginx resolves the API by
the compose service name `api` through `/etc/hosts`.

```bash
minicontainer pull nginx:alpine
minicontainer pull python:3.12-alpine
minicontainer build -t recipe-gateway-api:1 recipes/gateway/api
minicontainer build -t recipe-gateway-web:1 recipes/gateway/web
minicontainer compose -f recipes/gateway/compose.yml up
curl -fsS http://127.0.0.1:8080/ | grep -o 'gateway api ok'
minicontainer compose -f recipes/gateway/compose.yml ps
minicontainer compose -f recipes/gateway/compose.yml down
```

Expected output is in `expected.txt`. Both services use `network: bridge`; if the bridge cannot be
created, run the same containers with `network: host` and adjust the proxy target to `127.0.0.1:8000`.
