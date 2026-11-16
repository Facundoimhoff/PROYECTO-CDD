# =====================================================================
# ClimControl · carcasa para impresión 3D (script de Blender)
#
# Cómo correrlo (sin abrir Blender):
#   blender -b -P carcasa_climcontrol.py -- <carpeta de salida>
# Genera: base.stl, tapa.stl, carcasa_climcontrol.blend y las vistas .png
#
# Todas las medidas están en milímetros y arriba de todo, para cambiarlas.
# COOLER es el lado del cooler (80, 92 o 120).
# =====================================================================
import bpy, math, os, sys
from mathutils import Vector

# ----------------------------- MEDIDAS -----------------------------
COOLER         = 80.0   # lado del cooler (mm): 80, 92 o 120
COOLER_ESPESOR = 25.0   # grosor del cooler
PARED   = 2.4           # espesor de paredes (3 perímetros de 0,8 mm)
PISO    = 2.4
HOLGURA = 0.4

PROTO_L, PROTO_A, PROTO_H = 165.5, 55.5, 10.0     # protoboard completa (830 puntos)
RELE_L, RELE_A = 34.5, 28.5                        # módulo relé de 1 canal (medido: 34 × 28)
PIR_PCB_L, PIR_PCB_A, PIR_DOMO = 35.5, 28.5, 27.5  # HC-SR501: placa (medida: 35 × 28) y agujero para el domo (medido: Ø 25 + margen)

# Interior de la bandeja: protoboard + relé al lado, con lugar para los cables
GAP_USB = 9.0           # lugar entre la pared del USB y la protoboard (para el conector micro-USB)
INT_L = GAP_USB + PROTO_L + 4.0
INT_A = 7.0 + PROTO_A + 5.0 + RELE_A + 3.0
INT_H = 40.0            # altura interior: protoboard + ESP32 + cables dupont doblados

EXT_L = INT_L + 2 * PARED
EXT_A = INT_A + 2 * PARED
ALTO_BANDEJA = INT_H + PISO

# Soporte del cooler: una U abierta atrás y arriba, pegada al extremo de la bandeja
SOP_INT   = COOLER + 1.0
SOP_ANCHO = SOP_INT + 2 * PARED
SOP_PROF  = COOLER_ESPESOR + 3.0 + PARED       # cooler + aire + frente con rejilla
SOP_ALTO  = PISO + SOP_INT + 1.0
TORNILLOS = {80: 71.5, 92: 82.5, 120: 105.0}[int(COOLER)]   # distancia entre agujeros del cooler
ABERTURA  = {80: 76.0, 92: 88.0, 120: 115.0}[int(COOLER)]   # diámetro de la rejilla

# Posiciones (origen: esquina del piso del lado del USB, centro en Y)
PROTO_X0 = PARED + GAP_USB
PROTO_Y  = INT_A / 2 - 7.0 - PROTO_A / 2        # centro Y de la protoboard (pegada a la pared +Y)
RELE_X0  = PARED + 60.0
RELE_Y   = PROTO_Y - PROTO_A / 2 - 5.0 - RELE_A / 2
# El PIR va pinchado en la protoboard (sin cables): la tapa lleva un embudo que baja hasta el domo.
PIR_DESDE_USB   = 114.5   # distancia desde el extremo de la protoboard del lado del USB hasta el centro del domo (fila 44)
PIR_DESDE_BORDE = 25.0    # desde el borde largo de la protoboard que va contra la pared (el de las letras j, donde están los potenciómetros del PIR) hasta el centro del domo
PIR_DOMO_ALTO   = 30.0    # con la protoboard apoyada en la mesa: altura desde la mesa hasta la punta del domo (medir)
PIR_X = PROTO_X0 + PIR_DESDE_USB
PIR_Y = PROTO_Y + PROTO_A / 2 - PIR_DESDE_BORDE
EMBUDO_FONDO = PISO + PIR_DOMO_ALTO - 7.0      # fondo del embudo (2 mm de espesor): el domo asoma 5 mm por encima
EMBUDO_ANGULO = 40.0                          # inclinación de la pared del embudo respecto de la vertical
DHT_X = PROTO_X0 + 80.0                         # el DHT11 está pinchado en la protoboard (filas 29-33): ranuras de aire en la pared y en la tapa, encima de él
DHT_Y = PROTO_Y + PROTO_A / 2 - 20.0

JACK_12V = 11.5   # agujero para el jack de la fuente de 12 V
SALIDA = sys.argv[sys.argv.index("--") + 1] if "--" in sys.argv else os.path.dirname(os.path.abspath(__file__))
os.makedirs(SALIDA, exist_ok=True)
SOLAPE = 0.5   # las piezas que se suman se superponen un poco para que la unión sea limpia

# ----------------------------- AYUDAS ------------------------------
def limpiar():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    u = bpy.context.scene.unit_settings
    u.system, u.scale_length, u.length_unit = 'METRIC', 0.001, 'MILLIMETERS'

def caja(nombre, x0, y0, z0, lx, ly, lz):
    """Caja desde la esquina (x0, y0, z0) con lados lx, ly, lz. Su origen queda en el centro."""
    bpy.ops.mesh.primitive_cube_add(size=1, location=(x0 + lx / 2, y0 + ly / 2, z0 + lz / 2))
    o = bpy.context.active_object
    o.name = nombre
    o.scale = (lx, ly, lz)
    bpy.ops.object.transform_apply(scale=True)
    return o

def cilindro(nombre, cx, cy, cz, diametro, largo, eje='Z', lados=96):
    bpy.ops.mesh.primitive_cylinder_add(vertices=lados, radius=diametro / 2, depth=largo, location=(cx, cy, cz))
    o = bpy.context.active_object
    o.name = nombre
    if eje == 'X': o.rotation_euler = (0, math.pi / 2, 0)
    if eje == 'Y': o.rotation_euler = (math.pi / 2, 0, 0)
    bpy.ops.object.transform_apply(rotation=True)
    return o

def cono(nombre, cx, cy, z0, z1, r_abajo, r_arriba, lados=128):
    """Tronco de cono vertical entre z0 (radio r_abajo) y z1 (radio r_arriba)."""
    bpy.ops.mesh.primitive_cone_add(vertices=lados, radius1=r_abajo, radius2=r_arriba, depth=z1 - z0, location=(cx, cy, (z0 + z1) / 2))
    o = bpy.context.active_object
    o.name = nombre
    return o

def booleano(base, herramienta, op):
    m = base.modifiers.new(op, 'BOOLEAN')
    m.operation, m.solver, m.object = op, 'EXACT', herramienta
    bpy.context.view_layer.objects.active = base
    bpy.ops.object.modifier_apply(modifier=m.name)
    bpy.data.objects.remove(herramienta, do_unlink=True)

def unir(base, *piezas):
    for p in piezas: booleano(base, p, 'UNION')
def restar(base, *piezas):
    for p in piezas: booleano(base, p, 'DIFFERENCE')

def texto(nombre, contenido, cx, cy, z, alto_letra, espesor):
    bpy.ops.object.text_add(location=(cx, cy, z))
    t = bpy.context.active_object
    t.data.body, t.data.size, t.data.extrude = contenido, alto_letra, espesor / 2
    t.data.align_x = t.data.align_y = 'CENTER'
    bpy.ops.object.convert(target='MESH')
    t = bpy.context.active_object
    t.name = nombre
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    return t

def limpiar_malla(o):
    bpy.context.view_layer.objects.active = o
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.mesh.remove_doubles(threshold=0.01)
    bpy.ops.mesh.delete_loose()          # aristas o vértices sueltos que dejan los booleanos
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.mesh.normals_make_consistent(inside=False)
    bpy.ops.object.mode_set(mode='OBJECT')

def esquinas(nombre, x0, y0, lx, ly, alto=6.0, pata=8.0, grosor=2.0):
    """Cuatro topes en L alrededor de un rectángulo, dejando HOLGURA. Sujetan la pieza sin tornillos."""
    X0, X1, Y0, Y1 = x0 - HOLGURA, x0 + lx + HOLGURA, y0 - HOLGURA, y0 + ly + HOLGURA
    piezas = []
    for sx in (1, -1):
        for sy in (1, -1):
            # barra a lo largo de X (afuera del lado Y) y barra a lo largo de Y (afuera del lado X)
            xa = X0 - grosor if sx > 0 else X1 + grosor - pata
            ya = Y0 - grosor if sy > 0 else Y1
            piezas.append(caja(nombre, xa, ya, PISO - SOLAPE, pata, grosor, alto + SOLAPE))
            xb = X0 - grosor if sx > 0 else X1
            yb = Y0 - grosor if sy > 0 else Y1 + grosor - pata
            piezas.append(caja(nombre, xb, yb, PISO - SOLAPE, grosor, pata, alto + SOLAPE))
    return piezas

# ------------------------------- BASE -------------------------------
def hacer_base():
    base = caja("Base", 0, -EXT_A / 2, 0, EXT_L, EXT_A, ALTO_BANDEJA)
    restar(base, caja("hueco", PARED, -INT_A / 2, PISO, INT_L, INT_A, INT_H + 1))

    # Soporte del cooler: piso + dos mejillas + frente con rejilla, pegado al extremo de la bandeja
    x0 = EXT_L - SOLAPE
    unir(base,
         caja("sop_piso", x0, -SOP_ANCHO / 2, 0, SOP_PROF + SOLAPE, SOP_ANCHO, PISO),
         caja("sop_mej1", x0, -SOP_ANCHO / 2, 0, SOP_PROF + SOLAPE, PARED, SOP_ALTO),
         caja("sop_mej2", x0, SOP_ANCHO / 2 - PARED, 0, SOP_PROF + SOLAPE, PARED, SOP_ALTO),
         caja("sop_frente", EXT_L + SOP_PROF - PARED, -SOP_ANCHO / 2, 0, PARED, SOP_ANCHO, SOP_ALTO))

    # Rejilla del frente: un círculo grande al que se le dejan el cubo central, dos anillos y cuatro rayos
    cz = PISO + SOP_INT / 2
    fx = EXT_L + SOP_PROF - PARED / 2           # centro del espesor del frente
    agujero = cilindro("rej_agujero", fx, 0, cz, ABERTURA, PARED + 2, 'X')
    restar(agujero, cilindro("cubo", fx, 0, cz, 12.0, PARED + 4, 'X'))
    for r in (ABERTURA * 0.23, ABERTURA * 0.37):
        anillo = cilindro("anillo", fx, 0, cz, 2 * r + 2.2, PARED + 4, 'X')
        restar(anillo, cilindro("anillo_int", fx, 0, cz, 2 * r, PARED + 6, 'X'))
        restar(agujero, anillo)
    for ang in (0, 45, 90, 135):
        rayo = caja("rayo", fx - PARED / 2 - 2, -1.1, cz - ABERTURA / 2 - 1, PARED + 4, 2.2, ABERTURA + 2)
        rayo.rotation_euler = (math.radians(ang), 0, 0)   # gira sobre su centro, que es el centro de la rejilla
        bpy.ops.object.transform_apply(rotation=True)
        restar(agujero, rayo)
    restar(base, agujero)
    # Agujeros para los tornillos del cooler (Ø 4,3)
    for sy in (-1, 1):
        for sz in (-1, 1):
            restar(base, cilindro("tornillo", fx, sy * TORNILLOS / 2, cz + sz * TORNILLOS / 2, 4.3, PARED + 2, 'X'))
    # Paso del cable del cooler hacia la bandeja (abajo, en la pared del fondo)
    restar(base, caja("paso_cable", EXT_L - PARED - 1, 28, PISO, PARED + 2, 12, 8))

    # El piso queda totalmente plano: la protoboard se pega con su adhesivo y el relé con cinta doble faz

    # Abertura para el cable micro-USB de la ESP32 (pared del lado del USB, centrada en la protoboard)
    # Agujero de 20 × 16 mm, corrido 3 mm hacia el centro (el conector sigue quedando dentro del agujero)
    restar(base, caja("usb", -1, PROTO_Y - 13, PISO + 12, PARED + 2, 20, 16))
    # Ranuras de aire en la pared +Y, a la altura del DHT11 (así le llega el aire del aula)
    for i in range(-4, 5):
        restar(base, caja("ranura", DHT_X + i * 5 - 1, EXT_A / 2 - PARED - 1, PISO + 14, 2.0, PARED + 2, 22))
    # Ranuras de aire en la pared -Y (entrada de aire, frente a la ESP32)
    for i in range(10):
        restar(base, caja("ranura", PARED + 25 + i * 6, -EXT_A / 2 - 1, PISO + 16, 2.0, PARED + 2, 18))
    # Muesca para el cable de la fuente de 12 V (borde superior de la pared -Y, cerca del relé)
    # Ø 11,5 mm: entra un jack DC hembra de chasis (5,5 × 2,1 mm, rosca de 11 mm), donde se enchufa la fuente
    # de 12 V que va al tomacorriente. Si no usan jack, también sirve para pasar el cable de la fuente.
    restar(base, cilindro("jack12v", EXT_L - 16.0, EXT_A / 2 - PARED / 2, PISO + 20.0, JACK_12V, PARED + 2, 'Y'))
    limpiar_malla(base)
    return base

# ------------------------------- TAPA -------------------------------
def hacer_tapa():
    tapa = caja("Tapa", 0, -EXT_A / 2, 0, EXT_L, EXT_A, PARED)
    # Labio que entra en la bandeja y centra la tapa
    labio = caja("labio", PARED + HOLGURA / 2, -INT_A / 2 + HOLGURA / 2, -4.0, INT_L - HOLGURA, INT_A - HOLGURA, 4.0 + SOLAPE)
    restar(labio, caja("labio_int", PARED + HOLGURA / 2 + 1.6, -INT_A / 2 + HOLGURA / 2 + 1.6, -5, INT_L - HOLGURA - 3.2, INT_A - HOLGURA - 3.2, 6))
    unir(tapa, labio)
    # Embudo del PIR: un cono hueco que baja desde la tapa hasta justo sobre la placa del PIR,
    # con el agujero del domo en el fondo. Como se abre hacia arriba, desde el domo se ve
    # con el mismo ángulo que tiene el sensor (ver EMBUDO_ANGULO y el cálculo en el LEEME).
    prof = ALTO_BANDEJA - EMBUDO_FONDO            # cuánto baja el embudo por debajo de la tapa
    r_fondo = PIR_DOMO / 2 + 1.5                  # radio del anillo plano del fondo
    r_arriba = r_fondo + (prof + PARED) * math.tan(math.radians(EMBUDO_ANGULO))
    embudo_ext = cono("embudo_ext", PIR_X, PIR_Y, -prof, PARED, r_fondo + PARED, r_arriba + PARED)
    embudo_int = cono("embudo_int", PIR_X, PIR_Y, -prof + 2.0, PARED + 1, r_fondo, r_arriba + 1.0)
    unir(tapa, embudo_ext)
    restar(tapa, embudo_int)
    restar(tapa, cilindro("domo", PIR_X, PIR_Y, -prof + 1, PIR_DOMO, 6))
    assert PIR_Y + r_arriba + PARED < EXT_A / 2 and PIR_Y - r_arriba - PARED > -EXT_A / 2, "El embudo no entra en el ancho de la tapa: bajar EMBUDO_ANGULO"
    # Ranuras de aire sobre la ESP32 y sobre el DHT11
    for i in range(7):
        restar(tapa, caja("ranura", PARED + 22 + i * 6, PROTO_Y - 11, -1, 2.2, 22, PARED + 2))
    for i in range(-3, 4):
        restar(tapa, caja("ranura", DHT_X + i * 5 - 1.1, DHT_Y - 9, -1, 2.2, 18, PARED + 2))
    # Muesca para levantar la tapa (borde del lado del USB)
    restar(tapa, cilindro("muesca", 0, -INT_A / 4, 0, 18, PARED + 4))
    # Nombre en relieve
    unir(tapa, texto("logo", "ClimControl", EXT_L * 0.42, -INT_A / 4 + 2, PARED - SOLAPE + (0.8 + SOLAPE) / 2, 9.0, 0.8 + SOLAPE))
    limpiar_malla(tapa)
    return tapa

# ------------------------------ RENDER ------------------------------
def colorear(obj, color):
    """Color para las vistas (Workbench usa el color del objeto)."""
    obj.color = (*color, 1)
    m = bpy.data.materials.new(obj.name + "_mat")
    m.diffuse_color = (*color, 1)
    obj.data.materials.append(m)

def render(nombre, desde, hacia, orto=False, escala=260, sombras=True):
    esc = bpy.context.scene
    esc.render.engine = 'BLENDER_WORKBENCH'
    sh = esc.display.shading
    sh.light = 'STUDIO'
    sh.color_type = 'OBJECT'
    sh.show_cavity = True
    sh.cavity_type = 'BOTH'
    sh.show_shadows = sombras
    sh.background_type = 'VIEWPORT'
    sh.background_color = (0.90, 0.91, 0.93)
    esc.render.resolution_x, esc.render.resolution_y = 1400, 1000
    cam_data = bpy.data.cameras.new("cam")
    cam = bpy.data.objects.new("cam", cam_data)
    esc.collection.objects.link(cam)
    cam.location = Vector(desde)
    cam.rotation_euler = (Vector(hacia) - Vector(desde)).to_track_quat('-Z', 'Y').to_euler()
    if orto:
        cam_data.type, cam_data.ortho_scale = 'ORTHO', escala
    else:
        cam_data.lens = 45
    cam_data.clip_end = 5000
    esc.camera = cam
    esc.render.filepath = os.path.join(SALIDA, nombre)
    bpy.ops.render.render(write_still=True)
    bpy.data.objects.remove(cam, do_unlink=True)

def exportar_stl(obj, nombre):
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    ruta = os.path.join(SALIDA, nombre)
    if hasattr(bpy.ops.wm, "stl_export"):
        bpy.ops.wm.stl_export(filepath=ruta, export_selected_objects=True)
    else:
        bpy.ops.export_mesh.stl(filepath=ruta, use_selection=True)

# -------------------------------- MAIN --------------------------------
limpiar()
base = hacer_base()
tapa = hacer_tapa()
colorear(base, (0.80, 0.82, 0.86))
colorear(tapa, (0.18, 0.45, 0.85))

# Cooler y domo del PIR de referencia: solo para las vistas, no se exportan
cooler = caja("cooler_ref", EXT_L + 2.0, -COOLER / 2, PISO + 0.5, COOLER_ESPESOR, COOLER, COOLER)
restar(cooler, cilindro("cooler_hueco", EXT_L + 2.0 + COOLER_ESPESOR / 2, 0, PISO + 0.5 + COOLER / 2, COOLER - 4, COOLER_ESPESOR + 2, 'X'))
colorear(cooler, (0.12, 0.12, 0.13))
bpy.ops.mesh.primitive_uv_sphere_add(radius=PIR_DOMO / 2 - 0.5, location=(PIR_X, PIR_Y, PISO + PIR_DOMO_ALTO - PIR_DOMO / 2 + 0.5))
domo = bpy.context.active_object
domo.name = "pir_ref"
colorear(domo, (0.97, 0.97, 0.95))

tapa.location.z = ALTO_BANDEJA
render("vista_1_perspectiva.png", (-170, -260, 190), (EXT_L / 2 + 20, 0, 25))
render("vista_2_desde_el_cooler.png", (EXT_L + 320, -150, 120), (EXT_L / 2 + 30, 0, 30))
tapa.location.z = ALTO_BANDEJA + 70
render("vista_3_tapa_levantada.png", (-150, -250, 230), (EXT_L / 2 + 20, 0, 40))
tapa.hide_render = domo.hide_render = True
render("vista_4_interior_desde_arriba.png", (EXT_L / 2 + 15, -120, 330), (EXT_L / 2 + 15, 0, 0), orto=True, escala=250, sombras=False)
render("vista_5_interior.png", (60, -200, 160), (EXT_L / 2 + 10, 0, 10), sombras=False)
tapa.hide_render = domo.hide_render = False
tapa.location.z = ALTO_BANDEJA

# Exportar: la base como está y la tapa dada vuelta (se imprime con la cara de arriba apoyada)
import bmesh
def chequear(obj):
    bm = bmesh.new(); bm.from_mesh(obj.data)
    malos = sum(1 for e in bm.edges if not e.is_manifold)
    bm.free()
    print("CHEQUEO %s: %d caras, %d aristas abiertas (0 = malla cerrada, lista para imprimir)" % (obj.name, len(obj.data.polygons), malos))
chequear(base); chequear(tapa)
exportar_stl(base, "base.stl")
bpy.ops.object.select_all(action='DESELECT')
tapa.select_set(True)
bpy.context.view_layer.objects.active = tapa
bpy.ops.object.duplicate()
tapa_stl = bpy.context.active_object
tapa_stl.rotation_euler = (math.pi, 0, 0)
bpy.ops.object.transform_apply(location=True, rotation=True)
tapa_stl.location.z = -min(v.co.z for v in tapa_stl.data.vertices)
bpy.ops.object.transform_apply(location=True)
exportar_stl(tapa_stl, "tapa.stl")
bpy.data.objects.remove(tapa_stl, do_unlink=True)
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(SALIDA, "carcasa_climcontrol.blend"))
print("LISTO: bandeja %.1f x %.1f x %.1f mm; con el soporte del cooler: %.1f de largo y %.1f de alto"
      % (EXT_L, EXT_A, ALTO_BANDEJA, EXT_L + SOP_PROF, SOP_ALTO))
