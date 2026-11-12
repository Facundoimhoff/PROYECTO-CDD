# ClimControl con WiFi y Supabase

La ESP32 controla el aula sola y manda los datos por WiFi a **Supabase** (la base de datos en internet). La página (`index.html`, publicada en Vercel) lee de Supabase. No hace falta cable USB, ni computadora al lado, ni el conector de Access.

```
ESP32 (en el aula) ──WiFi──▶ Supabase (base de datos + cuentas) ◀── página en Vercel (cualquier navegador)
```

## Qué hay en esta carpeta

| Archivo | Para qué |
|---|---|
| `index.html` | La página: cuentas, Inicio, Aulas, Consumos, Consultas, Perfil y Configuración. |
| `supabase/climcontrol.sql` | La base de datos entera: tablas, seguridad, funciones y las 5 consultas. Se pega una vez en Supabase. |
| `ClimControl_WiFi/ClimControl_WiFi.ino` | El programa de la ESP32 con WiFi. |
| `ClimControl_WiFi/secretos.h` | Los datos privados de **esa** ESP32 (WiFi, código y clave). No se sube a GitHub. |
| `ClimControl.accdb`, `Conectar base de datos.bat` | La versión anterior, con Access y USB. Ya no se usan. |

## Puesta en marcha (una sola vez)

1. **Crear el proyecto en Supabase** (gratis): en supabase.com, *New project*. Nombre: `ClimControl`. Región: *South America (São Paulo)*. Guardá la contraseña de la base en un lugar seguro.
2. **Crear la base:** *SQL Editor* → *New query* → pegar todo `supabase/climcontrol.sql` → *Run*. Tiene que decir *Success*.
3. **Cuentas:**
   - En *Authentication* → *URL Configuration*, poné como *Site URL* `https://climcontrol.vercel.app` y agregala también en *Redirect URLs*.
   - Si no querés que haga falta confirmar el mail, en *Authentication* → *Sign In / Providers* → *Email* apagá *Confirm email*. El plan gratis manda pocos mails por hora.
4. **Conectar la página:** en *Project Settings* → *API Keys* copiá la *Project URL* y la clave **publishable** (o *anon*). **Nunca la secret.** Van en `index.html`, en `SUPABASE_URL` y `SUPABASE_CLAVE`.
5. **Publicar:** subir `index.html` a GitHub. Vercel la publica solo.

## Agregar un aula con su ESP32

1. En la página: crear la cuenta → **Aulas** → **Agregar aula** → **Copiar secretos.h**. La clave se muestra una sola vez.
2. En Arduino IDE abrir `ClimControl_WiFi/ClimControl_WiFi.ino`. En la pestaña `secretos.h` pegar lo copiado y completar el WiFi. Tiene que ser una red de **2,4 GHz**. Las redes con página de inicio de sesión, como suelen ser las de la facultad, no sirven: para probar, usá el WiFi compartido del celular.
3. Subir el programa a la ESP32. En el Serial Monitor tiene que aparecer `Supabase: datos enviados`. En la página, el aula pasa a **En línea**.
4. Desde ahí, la ESP32 se puede enchufar a cualquier cargador de 5 V. El cooler sigue con su fuente de 12 V.

## Cómo funciona

- **Cada 10 segundos** (y apenas cambia un equipo o la presencia), la ESP32 llama a la función `dispositivo_reportar`. Esa función guarda la lectura, registra los eventos y le devuelve las temperaturas de la Configuración.
- **Temperaturas desde la página:** la ESP32 las guarda en su memoria. Sin internet sigue con las últimas que recibió; si nunca recibió ninguna, usa 24 °C y 27 °C.
- **Seguridad:**
  - Cada cuenta ve solo sus aulas (RLS).
  - La ESP32 no usa una cuenta: se identifica con el código y la clave de su dispositivo. La clave se guarda cifrada (SHA-256).
- **Consumo:** se suma el tiempo que estuvo prendido cada equipo y se multiplica por su potencia. El aire gasta más cuanto más calor hace (tres rangos). El precio del kWh viene con un valor de referencia para Córdoba y se cambia en Configuración.

## Límites del plan gratis de Supabase

- 500 MB de base. Cada aula guarda unas 8.640 lecturas por día (cerca de 1 MB), así que alcanza para varios meses con varias aulas.
- Un proyecto sin actividad se pausa a la semana. Mientras haya una ESP32 mandando datos, eso no pasa.
