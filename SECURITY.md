# Seguridad

## Archivos privados

Nunca publiques:

- `arduino_secrets.h`
- `fire_event_config.h`
- `X-User-Token`
- `x-project-uuid`
- UUID reales de workflows
- creator IDs
- coordenadas operativas

Ambos archivos privados están incluidos en `.gitignore`. Las variantes
`.example.h` contienen únicamente marcadores.

Si un token aparece en un commit, eliminarlo posteriormente no es suficiente:
revócalo en FlightHub 2, genera uno nuevo y revisa el historial.

## Red

El Opta inicia una conexión HTTPS saliente hacia FlightHub 2. No abras el
dashboard directamente a Internet ni configures port forwarding hacia el
puerto 80. Para acceso remoto utiliza una VPN o una pasarela autenticada.

## Operación

- Prueba primero en DRY RUN.
- Mantén I1-I4 en OFF durante el arranque y la carga.
- Confirma que sólo una entrada de evento esté activa.
- Verifica el efecto del workflow antes de habilitar envío real.
- Conserva una forma independiente de detener las operaciones del dron.

## Reporte

Reporta vulnerabilidades de manera privada al propietario del repositorio. No
incluyas tokens, identificadores o ubicaciones reales.
