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
  - **USB:** en la pared corta del lado de la protoboard, a la altura del conector de la ESP32 (18 × 13 mm).
  - **DHT11:** queda pinchado en la protoboard como está; justo encima tiene ranuras de aire en la tapa y en la pared larga, para que le llegue el aire del aula. Dentro de la caja va a medir 1 o 2 °C más que afuera (el calor de la ESP32): si molesta, se compensa bajando 1 °C las temperaturas en Configuración.
  - **Fuente de 12 V:** muesca en el borde superior de la otra pared larga, a la altura del relé; la tapa la cierra.
  - **Ranuras de aire** también en la tapa sobre la ESP32 y en la otra pared larga.
- **Topes.** La protoboard y el relé apoyan entre cuatro topes en L cada uno: no hacen falta tornillos (la protoboard además tiene su adhesivo).
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
| Domo del PIR | Ø 23 mm (agujero de 26 para que no haga falta ser exacto) | cambiar `PIR_DOMO` |

Para regenerar los STL después de cambiar una medida:

```
blender -b -P carcasa_climcontrol.py -- .
```

## Armado

1. Pegar la protoboard en el piso, entre sus topes, con el extremo de la ESP32 hacia la pared del USB (la que tiene la abertura rectangular) y el borde de las letras **j** (el lado de los potenciómetros del PIR) contra la pared larga que tiene las ranuras del DHT11.
2. Apoyar el relé entre sus topes.
3. El DHT11 queda donde está, pinchado en la protoboard.
4. Entrar el cooler desde arriba en el soporte, con los cables hacia abajo, y atornillarlo desde el frente. Pasar el cable por el hueco bajo hacia la bandeja.
5. Pasar el cable de la fuente de 12 V por la muesca y conectarlo al relé.
6. Dejar el PIR pinchado en la protoboard y cerrar la tapa: el domo asoma por el fondo del embudo.
7. Enchufar el USB por la abertura de la pared corta.
