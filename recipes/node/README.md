# Node recipe

A minimal HTTP server on node:20-alpine.

```bash
minicontainer pull node:20-alpine
minicontainer build -t recipe-node:1 recipes/node
minicontainer run -d --name recipe-node --image=recipe-node:1 --ram=96M --net=bridge \
  -p 3000:3000 node /app/server.js
curl -fsS http://127.0.0.1:3000/ | grep -o 'node recipe ok'
minicontainer logs recipe-node
minicontainer stop recipe-node && minicontainer rm recipe-node
```

Expected output is in `expected.txt`. The base image is large; give the container at least 96 MB.
