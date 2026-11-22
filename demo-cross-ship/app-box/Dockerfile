FROM python:3.12-alpine
# COPY instead of RUN: works for any --platform (RUN would need qemu/binfmt)
COPY resolv.conf /etc/resolv.conf
COPY weather.py /app/weather.py
CMD ["python", "/app/weather.py"]
