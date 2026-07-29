# Troubleshooting

## No aparece una IP

- Revisa enlace Ethernet.
- Confirma que existe DHCP.
- Abre el monitor serie a 115200 baud.
- Reinicia el Opta con I1-I4 en OFF.

## El dashboard no abre

- Usa `http://<IP-del-Opta>`, no `127.0.0.1`.
- Confirma que el navegador esté en la misma LAN.
- Prueba `http://<IP>/api/status`.
- Revisa firewall y aislamiento de clientes Wi-Fi.

## I2-I4 no cambian

- Mide el voltaje respecto a GND.
- Confirma que ON supere 6.6 V.
- Revisa la asignación A1, A2 y A3.
- No apliques tensiones fuera de especificación.

## Evento bloqueado

- I1 debe estar ON.
- Sólo una entrada I2-I4 puede estar activa.
- Libera completamente las entradas.
- Espera el cooldown indicado.

## HTTP 0

No hubo una respuesta HTTP válida. Revisa:

- DNS.
- NTP y fecha UTC.
- Salida TCP/443.
- CA instalada.
- Credenciales privadas.

## HTTP 401 o 403

- Token incorrecto o revocado.
- Proyecto equivocado.
- Permisos insuficientes.
- Encabezados mal configurados.

## HTTP 2xx pero no despega

Un HTTP exitoso confirma aceptación del endpoint, no que el dron pueda iniciar
una misión. Revisa el workflow, Dock, disponibilidad, batería, clima,
restricciones de vuelo y reglas operativas.

## La prioridad no coincide

`params.level` puede ser sólo un dato para el workflow. Revisa que el workflow
lo interprete y no asumas preempción de una misión ya iniciada.
