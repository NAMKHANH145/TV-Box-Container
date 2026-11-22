FROM python:3.12-alpine
# COPY instead of RUN: works for any --platform (RUN would need qemu/binfmt)
COPY resolv.conf /etc/resolv.conf
COPY repoinfo.py /app/repoinfo.py
CMD ["python", "/app/repoinfo.py"]
