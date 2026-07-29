# Operación y dashboard

## Secuencia normal

1. I1 pasa a ON y arma el sistema.
2. Se activa solamente I2, I3 o I4.
3. La entrada permanece estable durante dos segundos.
4. El Opta crea un evento con su nombre, nivel y coordenadas.
5. En modo real abre TLS y envía el POST.
6. El dashboard muestra resultado, HTTP y hora UTC.
7. Se liberan todas las entradas de evento.
8. Comienza el cooldown global.

## Entradas simultáneas

La versión segura no envía nada cuando detecta dos o tres entradas de evento
activas. El dashboard y el monitor serie informan el bloqueo. Esta decisión
evita que un fallo común o un error de cableado genere varias misiones.

## Dashboard

El Opta sirve la página en el puerto 80 y expone:

```text
GET /
GET /api/status
```

El navegador consulta `/api/status` cada 350 ms mediante `fetch`. La página no
se recarga y muestra:

- I1-I4 y sus voltajes.
- Fase del evento.
- Tipo y coordenadas transmitidas.
- Código HTTP.
- Contadores totales y por tipo.
- Cooldown restante.
- Estado de comunicación local.

No existen endpoints web para disparar eventos.

## Estados

```text
LISTO -> ARMADO -> CONFIRMANDO -> GENERADO -> ENVIANDO -> ENVIADO
                                           \-> ERROR
```
