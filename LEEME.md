# ClimControl con WiFi y Supabase

El aparato (una ESP32) controla el aula solo y manda los datos por WiFi a **Supabase** (la base de datos en internet). La página (`index.html`, publicada en Vercel) lee de Supabase. No hace falta cable USB, ni computadora al lado, ni Arduino IDE para el cliente.

```
Aparato ClimControl (en el aula) ──WiFi──▶ Supabase (base de datos + cuentas) ◀── página en Vercel (celular o compu)
```

## Qué hay en esta carpeta

| Archivo | Para qué |
|---|---|
| `index.html` | La página: cuentas, Inicio, Aulas, Consumos, Consultas, Perfil y Configuración. |
| `supabase/climcontrol.sql` | La base de datos: tablas, seguridad, funciones y las 5 consultas. |
| `supabase/02_vinculacion.sql` | Agrega el código de vinculación (conectar un aparato sin Arduino IDE). |
| `ClimControl_WiFi/ClimControl_WiFi.ino` | El programa del aparato. Se carga **una vez, en fábrica**. |
| `ClimControl_WiFi/secretos.h` | Opcional, solo para pruebas: datos cargados a mano. No se sube a GitHub. |
| `ClimControl.accdb`, `Conectar base de datos.bat` | Versión anterior con Access y USB. Ya no se usan. |

## Puesta en marcha (una sola vez, la hacen ustedes)

1. **Supabase:** proyecto creado. En *SQL Editor* correr, en este orden:
   - `supabase/climcontrol.sql` (ya hecho);
   - `supabase/02_vinculacion.sql`.

   Los dos se pueden volver a correr sin perder datos.
2. **Cuentas:** en *Authentication* → *URL Configuration*, poner `https://climcontrol.vercel.app` como *Site URL* y en *Redirect URLs*. Si no quieren que haga falta confirmar el mail, apagar *Confirm email* en *Authentication* → *Sign In / Providers* → *Email*.
3. **Página:** ya tiene la URL y la clave pública de Supabase. Se publica sola en Vercel al subirla a GitHub.
4. **En fábrica, por cada aparato:** abrir `ClimControl_WiFi.ino` en Arduino IDE y subirlo a la ESP32. No hay que completar nada: la dirección de Supabase ya está en el programa.

## Lo que hace el cliente (sin Arduino IDE)

1. Crea su cuenta en la página y toca **Aulas → Agregar aula**. La página le da un **código de vinculación** de 8 caracteres, por ejemplo `K7P2-9QXM`. Vence en 30 minutos.
2. Enchufa el aparato en el aula (cargador de 5 V; el cooler va con su fuente de 12 V).
3. Con el celular se conecta a la red WiFi **ClimControl-XXXX** que crea el aparato. Se abre sola una página; si no, entra a `192.168.4.1`.
4. Elige el WiFi del aula, escribe su clave y el código. En menos de un minuto el aula aparece **En línea** en la app, y la red ClimControl-XXXX se apaga sola.

**Cambiar de WiFi o de cuenta:** mantener apretado el botón **BOOT** de la placa 5 segundos. El aparato borra el WiFi y la vinculación y vuelve a crear su red. Después, en la app, **Aulas → Dispositivo → Vincular un aparato** da un código nuevo.

El aparato también abre su red solo en dos casos:
- Si pasa un minuto sin poder conectarse al WiFi guardado, para que se pueda corregir la clave.
- Si el servidor deja de reconocerlo, por ejemplo porque borraron el aula.

## Para probar sin vincular (opcional)

En la app: **Aulas → Dispositivo → Cargarlo con Arduino IDE** da un `secretos.h` con el WiFi, el código y la clave. Se pega en `ClimControl_WiFi/secretos.h` y se sube el programa.

## Cómo funciona

- **Cada 10 segundos** (y apenas cambia un equipo o la presencia), el aparato llama a la función `dispositivo_reportar`. Esa función guarda la lectura, registra los eventos y le devuelve las temperaturas de la Configuración.
- **Temperaturas y vinculación:** el aparato las guarda en su memoria. Sin internet sigue funcionando con las últimas temperaturas que recibió (de fábrica: 24 °C y 27 °C).
- **Seguridad:**
  - Cada cuenta ve solo sus aulas (RLS).
  - El aparato no usa una cuenta: se identifica con su código y su clave propia, que se guarda cifrada (SHA-256).
  - El código de vinculación sirve una sola vez y vence en 30 minutos.
- **Consumo:** se suma el tiempo que estuvo prendido cada equipo y se multiplica por su potencia. El aire gasta más cuanto más calor hace (tres rangos). El precio del kWh es una referencia para Córdoba y se cambia en Configuración.

## Límites del plan gratis de Supabase

- 500 MB de base. Cada aula guarda unas 8.640 lecturas por día (cerca de 1 MB), así que alcanza para varios meses con varias aulas.
- Un proyecto sin actividad se pausa a la semana. Mientras haya un aparato mandando datos, eso no pasa.
