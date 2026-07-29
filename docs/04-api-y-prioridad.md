# API y prioridad

## Endpoint

```text
POST https://es-flight-api-us.djigate.com/openapi/v0.1/workflow
```

Encabezados:

```text
Content-Type: application/json
X-User-Token: <secreto>
x-project-uuid: <secreto>
```

## Payload

Ejemplo sanitizado:

```json
{
  "workflow_uuid": "<workflow>",
  "trigger_type": 0,
  "name": "Alarma de incendio - Arduino Opta",
  "params": {
    "creator": "<creator>",
    "latitude": 0.0,
    "longitude": 0.0,
    "level": 5,
    "desc": "Alarma generada por pulsador físico"
  }
}
```

## Niveles del firmware

| Evento | Nivel |
|---|---:|
| Incendio | 5 |
| Evento sísmico | 4 |
| Intrusión | 3 |

El campo `level` se transmite correctamente, pero el firmware no interpreta
ese número como una orden de vuelo. Tampoco se debe asumir que FlightHub 2
reordena o interrumpe misiones automáticamente: el comportamiento depende del
workflow y de las reglas de disponibilidad del dron o Dock.

## Experimento de prioridad

La versión principal bloquea entradas simultáneas. Para una prueba de
prioridad se recomienda una variante separada que:

1. Capture un lote estable de eventos.
2. Envíe primero nivel 3, luego 4 y al final 5.
3. Registre timestamps, HTTP y respuesta de cada solicitud.
4. Utilice un proyecto de prueba y una zona de vuelo controlada.
5. Compare el orden de envío con el orden realmente adoptado por FlightHub 2.

Enviar nivel 5 primero sesgaría el resultado. Una misión ya iniciada podría no
ser interrumpida aunque llegue posteriormente una alarma de mayor nivel.

## TLS

El firmware sincroniza UTC con NTP y valida el certificado del endpoint usando
Amazon Root CA 1. No incluye un modo para desactivar la validación.
