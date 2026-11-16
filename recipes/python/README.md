# Python recipe

A stdlib-only HTTP server on python:3.12-alpine.

```bash
minicontainer pull python:3.12-alpine
minicontainer build -t recipe-python:1 recipes/python
minicontainer run -d --name recipe-python --image=recipe-python:1 --ram=96M --net=bridge \
  -p 8000:8000 python /app.py
curl -fsS http://127.0.0.1:8000/ | grep -o 'python recipe ok'
minicontainer logs recipe-python
minicontainer stop recipe-python && minicontainer rm recipe-python
```

Expected output is in `expected.txt`. No pip install and no network access are needed at runtime.
