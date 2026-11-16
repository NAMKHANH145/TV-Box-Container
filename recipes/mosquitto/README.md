# Mosquitto recipe

An MQTT broker with a minimal, anonymous-allowed config for a trusted LAN.

```bash
minicontainer pull eclipse-mosquitto:2
minicontainer build -t recipe-mosquitto:1 recipes/mosquitto
minicontainer run -d --name mqtt --image=recipe-mosquitto:1 --ram=32M --net=bridge \
  -p 1883:1883 mosquitto -c /mosquitto/config/mosquitto.conf
minicontainer logs mqtt
minicontainer stop mqtt && minicontainer rm mqtt
```

Verify from the host with `mosquitto_pub -h 127.0.0.1 -t demo -m hello` and
`mosquitto_sub -h 127.0.0.1 -t demo -C 1`.

`mosquitto.conf` permits anonymous clients **only for this demo**. Put authentication and TLS in
front before exposing port 1883 outside the LAN.
