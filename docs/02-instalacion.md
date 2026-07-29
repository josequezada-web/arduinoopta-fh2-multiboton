# Instalación

## 1. Arduino IDE

Instala:

- Arduino IDE 2.x.
- Plataforma `Arduino Mbed OS Opta Boards`.
- Selecciona `Arduino Opta`.

El firmware utiliza `PortentaEthernet`, `Ethernet`, `EthernetSSLClient`,
`EthernetUDP` y el RTC de Mbed incluidos en la plataforma Opta.

## 2. Credenciales

Dentro de la carpeta del sketch:

```text
arduino_secrets.example.h  -> arduino_secrets.h
fire_event_config.example.h -> fire_event_config.h
```

Completa:

```cpp
constexpr char FH2_USER_TOKEN[] = "...";
constexpr char FH2_PROJECT_UUID[] = "...";
```

Y en `fire_event_config.h`:

```cpp
constexpr bool FH2_LIVE_SEND_ENABLED = false;
constexpr char WORKFLOW_UUID[] = "...";
constexpr char CREATOR_ID[] = "...";
constexpr char I2_EVENT_LATITUDE[] = "...";
constexpr char I2_EVENT_LONGITUDE[] = "...";
constexpr char I3_EVENT_LATITUDE[] = "...";
constexpr char I3_EVENT_LONGITUDE[] = "...";
constexpr char I4_EVENT_LATITUDE[] = "...";
constexpr char I4_EVENT_LONGITUDE[] = "...";
```

## 3. Primera carga

1. Pon I1-I4 en OFF.
2. Conserva el modo DRY RUN.
3. Compila y carga.
4. Abre el monitor serie a 115200 baud.
5. Confirma DHCP y NTP.
6. Abre el dashboard usando la IP impresa.

## 4. Envío real

Después de completar el plan de pruebas cambia deliberadamente:

```cpp
constexpr bool FH2_LIVE_SEND_ENABLED = true;
```

Vuelve a poner I1-I4 en OFF antes de cargar el firmware real.

## IP estática

La plataforma Opta admite:

```cpp
IPAddress ip(192, 168, 1, 50);
IPAddress dns(192, 168, 1, 1);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
Ethernet.begin(ip, dns, gateway, subnet);
```

Comprueba primero que la dirección esté libre y fuera del pool DHCP. Una
reserva DHCP suele ser más segura.
