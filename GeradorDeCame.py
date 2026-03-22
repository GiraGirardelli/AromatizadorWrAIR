import adsk.core, adsk.fusion, traceback, math

def run(context):
    ui = None
    try:
        app = adsk.core.Application.get()
        ui  = app.userInterface
        design = adsk.fusion.Design.cast(app.activeProduct)
        root = design.rootComponent
        sketch = root.sketches.add(root.xYConstructionPlane)
        
        # --- PARÂMETROS (EM CM) ---
        r_min = 1.8    # Raio inicial (18mm) - Mais robusto para o horn
        lift = 1.0     # Curso total (10mm) - Dobro do anterior
        angle_deg = 90 # Mais ângulo para maior precisão de ajuste
        # -------------------------------------

        points = adsk.core.ObjectCollection.create()
        
        for i in range(angle_deg + 1):
            theta = math.radians(i)
            # R(theta) = 1.8 + (1.0 / 1.57) * theta
            r = r_min + (lift / math.radians(angle_deg)) * theta
            points.add(adsk.core.Point3D.create(r * math.cos(theta), r * math.sin(theta), 0))
            
        sketch.sketchCurves.sketchFittedSplines.add(points)
        ui.messageBox(f'Novo Came de {lift*10}mm gerado. Use o código para limitar o curso!')

    except:
        if ui: ui.messageBox('Falha:\n{}'.format(traceback.format_exc()))
