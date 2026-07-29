# Arquitectura y cableado

## Componentes

- Arduino Opta WiFi AFX00002.
- Fuente regulada de 24 VDC.
- Interruptores o contactos secos adaptados mediante DIN Simul8.
- Ethernet con acceso a DNS, NTP y TCP/443.

## Asignación

| Terminal Opta | Uso |
|---|---|
| I1 / A0 | Armado general |
| I2 / A1 | Incendio |
| I3 / A2 | Intrusión |
| I4 / A3 | Evento sísmico |
| D0-D3 | Bloqueados en OFF |

Las entradas se leen en modo analógico y se consideran ON a partir de 6.6 V.
El firmware utiliza una escala aproximada de 0–10.88 V con ADC de 12 bits.

## Reglas eléctricas

- Comparte correctamente la referencia GND de 24 V.
- Verifica polaridad antes de energizar.
- No excedas las especificaciones de entrada del Opta.
- No utilices los relevadores como parte de un circuito certificado de
  seguridad.
- Antes de conectar una central real, utiliza aislamiento adecuado y revisa
  si sus salidas son de contacto seco, colector abierto o tensión.

## Red

El firmware usa DHCP por defecto. El monitor serie imprime:

```text
Dashboard: http://192.168.x.x
```

Para producción es preferible crear una reserva DHCP. Una IP estática dentro
del sketch también requiere DNS, gateway y máscara correctos para conservar
NTP y FlightHub 2.
