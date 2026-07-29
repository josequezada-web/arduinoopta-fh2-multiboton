# Plan de pruebas

## Etapa 1: hardware

- [ ] I1-I4 en OFF al arrancar.
- [ ] Cada entrada OFF mide cerca de 0 V.
- [ ] Cada entrada ON supera 6.6 V.
- [ ] D0-D3 permanecen apagados.

## Etapa 2: red

- [ ] DHCP asigna una IP.
- [ ] El dashboard abre desde otro equipo de la LAN.
- [ ] DNS resuelve el host de FlightHub 2.
- [ ] NTP establece una hora UTC correcta.
- [ ] TLS conecta en TCP/443.

## Etapa 3: DRY RUN

Para cada I2-I4:

- [ ] I1 debe estar ON.
- [ ] Una pulsación menor de dos segundos no genera evento.
- [ ] Una activación de dos segundos genera un solo evento.
- [ ] El JSON contiene el nombre esperado.
- [ ] El JSON contiene el nivel esperado.
- [ ] Las coordenadas corresponden a la entrada.
- [ ] Es obligatorio liberar la entrada.

## Etapa 4: protecciones

- [ ] I2 con I1 OFF queda bloqueado.
- [ ] I2 e I3 simultáneos quedan bloqueados.
- [ ] El cooldown impide una segunda solicitud.
- [ ] Una pérdida de TLS produce estado ERROR.
- [ ] Ningún fallo activa D0-D3.

## Etapa 5: FlightHub 2

- [ ] Utiliza un proyecto controlado.
- [ ] Envía primero una sola alarma.
- [ ] Confirma HTTP 2xx.
- [ ] Confirma que el evento aparece en FlightHub 2.
- [ ] Verifica manualmente nombre, nivel y destino.
- [ ] Repite con los otros dos tipos.

No hagas la primera prueba real con múltiples misiones ni con personas dentro
del área operacional.
