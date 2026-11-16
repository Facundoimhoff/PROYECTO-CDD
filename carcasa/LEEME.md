# Carcasa de ClimControl para impresión 3D

Dos piezas, modeladas en Blender con el script `carcasa_climcontrol.py`:

| Archivo | Qué es | Medidas |
|---|---|---|
| `base.stl` | Bandeja para la protoboard y el relé, con el soporte del cooler en un extremo | 216 × 104 mm, 42 mm de alto la bandeja y 84 mm el soporte del cooler |
| `tapa.stl` | Tapa con el embudo del PIR, ranuras de aire y el nombre en relieve | 185 × 104 × 21 mm |
| `carcasa_climcontrol.blend` | El modelo, por si quieren retocarlo en Blender | |
| `vista_*.png` | Vistas del modelo | |

## Cómo está pensada

- **El PIR queda pinchado en la protoboard, como está.** Como su domo queda a unos 28 mm de la mesa y la tapa está a 45 mm, la tapa tiene un **embudo**: un plato cónico que baja hasta 5 mm por debajo de la punta del domo, con el agujero en el fondo. El domo asoma por ese agujero y las paredes del embudo se abren a 40°, así que desde el domo se ve el cielo con un ángulo de ±52°, parecido al del sensor (±55°). No hace falta cambiar ningún cable. Si prefieren más ángulo, con 3 cables hembra-macho el PIR se puede subir a la tapa (modo anterior, está en el historial del script).
- **Cooler encastrado.** En un extremo hay un soporte en U, abierto arriba y atrás: el cooler entra desde arriba y se atornilla desde el frente con sus 4 tornillos (agujeros de 4,3 mm a 71,5 mm, lo normal en un cooler de 80 mm). El frente tiene la rejilla de aire. El cable del cooler pasa a la bandeja por un hueco bajo, en la pared del fondo, así no se ve.
- **Nada de cables a la vista.** Todo lo demás (protoboard, ESP32, relé, cables dupont, cable de 12 V) queda bajo la tapa. Aberturas:
  - **USB (5 V):** agujero de 20 × 16 mm en la pared corta, a la altura del micro-USB de la ESP32 y corrido 3 mm hacia el centro.
  - **DHT11:** queda pinchado en la protoboard como está; justo encima tiene ranuras de aire en la tapa y en la pared larga, para que le llegue el aire del aula. Dentro de la caja va a medir 1 o 2 °C más que afuera (el calor de la ESP32): si molesta, se compensa bajando 1 °C las temperaturas en Configuración.
  - **Fuente de 12 V:** agujero redondo de 11,5 mm en la pared larga, al lado del cooler (a su derecha, mirándolo de frente). Ahí va un **jack DC hembra de chasis** (5,5 × 2,1 mm, de rosca de 11 mm, se consigue en cualquier casa de electrónica): la fuente de 12 V se enchufa al tomacorriente y su ficha entra en ese jack. Si no consiguen el jack, el agujero también sirve para pasar el cable de la fuente.
  - **Ranuras de aire** también en la tapa sobre la ESP32 y en la otra pared larga.
- **Piso totalmente plano.** La protoboard se pega con su adhesivo de abajo y el relé con un pedazo de cinta doble faz, al lado de la protoboard. Así se pueden acomodar como queden mejor con los cables.
- **Lugar para los cables.** Por dentro mide 172 × 99 mm y 40 mm de alto: entran la protoboard (165 × 55), el relé suelto (34 × 28) al lado y los cables dupont parados (llegan a unos 26 mm).
- La tapa tiene un labio que entra en la bandeja y una muesca para levantarla con el dedo.

## Impresión

- **Material:** PLA o PETG. Boquilla 0,4 mm, capa 0,2 mm, 3 perímetros, relleno 15 %.
- **Base:** se imprime apoyada en su piso, tal como viene el STL. No necesita soportes: la rejilla del cooler está dividida en tramos cortos que se puentean bien.
- **Tapa:** el STL ya viene dada vuelta, con la cara de arriba apoyada en la cama; el embudo queda como un cono parado que se imprime sin soportes.
- **Cama:** la base mide 214 mm de largo: entra en una cama de 220 × 220 (Ender 3 y similares). Si la impresora es más chica, avisen y la cortamos en dos piezas.
- Tiempo aproximado: base 6 a 8 horas, tapa 2 a 3 horas.

## Medidas que se asumieron (revisar antes de imprimir)

| Pieza | Medida asumida | Si es distinta |
|---|---|---|
| Cooler | 80 × 80 × 25 mm, agujeros a 71,5 mm | cambiar `COOLER` a 92 o 120 en el script |
| Protoboard | completa, 165,5 × 55,5 mm | cambiar `PROTO_L` y `PROTO_A` |
| Módulo relé | 34 × 28 mm (medido) | cambiar `RELE_L` y `RELE_A` |
| PIR, posición en la protoboard | domo en la fila 44 (114,5 mm del extremo del USB) y a 25 mm del borde de las letras j (sacado de la foto) | cambiar `PIR_DESDE_USB` y `PIR_DESDE_BORDE` |
| PIR, altura | 30 mm (medido) de la mesa a la punta del domo (protoboard apoyada en la mesa) | cambiar `PIR_DOMO_ALTO` |
| Domo del PIR | Ø 25 mm (medido; agujero de 27,5 para que no haga falta ser exacto) | cambiar `PIR_DOMO` |

Para regenerar los STL después de cambiar una medida:

```
blender -b -P carcasa_climcontrol.py -- .
```

## Armado

1. Pegar la protoboard en el piso, a 1 cm de la pared corta que tiene el agujero rectangular del USB, con el extremo de la ESP32 hacia ese agujero, y el borde de las letras **j** (el lado de los potenciómetros del PIR) contra la pared larga que tiene las ranuras del DHT11.
2. Pegar el relé con cinta doble faz en el espacio libre al lado de la protoboard.
3. El DHT11 queda donde está, pinchado en la protoboard.
4. Entrar el cooler desde arriba en el soporte, con los cables hacia abajo, y atornillarlo desde el frente. Pasar el cable por el hueco bajo hacia la bandeja.
5. Atornillar el jack de 12 V en el agujero redondo al lado del cooler. Del jack sale el **+** al borne COM del relé, el borne NO del relé va al cable rojo del cooler y el cable negro del cooler va al **−** del jack.
6. Dejar el PIR apoyado en la protoboard, como está, y cerrar la tapa: el domo asoma por el fondo del embudo.
7. Enchufar el USB por la abertura de la pared corta.
