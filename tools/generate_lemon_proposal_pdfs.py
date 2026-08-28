from pathlib import Path
from shutil import copyfile

from reportlab.lib import colors
from reportlab.lib.enums import TA_RIGHT
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import mm
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (
    KeepTogether,
    PageBreak,
    Paragraph,
    SimpleDocTemplate,
    Spacer,
    Table,
    TableStyle,
)


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "output" / "pdf"
PUBLIC = ROOT / "landing" / "docs"
FONT_DIR = ROOT / "landing" / "assets" / "v2"
LEMON_LOGO = ROOT / "landing" / "assets" / "lemonlogo.png"

INK = colors.HexColor("#171917")
MUTED = colors.HexColor("#626962")
GREEN = colors.HexColor("#009B1A")
PALE = colors.HexColor("#F2F4EF")
LINE = colors.HexColor("#D9DDD6")


def register_fonts():
    pdfmetrics.registerFont(TTFont("Satoshi", "C:/Windows/Fonts/arial.ttf"))
    pdfmetrics.registerFont(TTFont("Satoshi-Bold", "C:/Windows/Fonts/arialbd.ttf"))
    pdfmetrics.registerFont(TTFont("Machina", str(FONT_DIR / "PPNeueMachina-PlainBold.ttf")))


def styles():
    sheet = getSampleStyleSheet()
    return {
        "kicker": ParagraphStyle("Kicker", parent=sheet["Normal"], fontName="Satoshi-Bold", fontSize=8, leading=10, textColor=GREEN, spaceAfter=6),
        "title": ParagraphStyle("Title", parent=sheet["Title"], fontName="Machina", fontSize=27, leading=29, tracking=-0.5, textColor=INK, spaceAfter=10),
        "subtitle": ParagraphStyle("Subtitle", parent=sheet["Normal"], fontName="Satoshi", fontSize=10, leading=15, textColor=MUTED, spaceAfter=18),
        "h2": ParagraphStyle("H2", parent=sheet["Heading2"], fontName="Machina", fontSize=16, leading=19, textColor=INK, spaceBefore=18, spaceAfter=10),
        "h3": ParagraphStyle("H3", parent=sheet["Heading3"], fontName="Satoshi-Bold", fontSize=10, leading=13, textColor=INK, spaceBefore=10, spaceAfter=4),
        "body": ParagraphStyle("Body", parent=sheet["BodyText"], fontName="Satoshi", fontSize=8.7, leading=13, textColor=MUTED, spaceAfter=7),
        "small": ParagraphStyle("Small", parent=sheet["Normal"], fontName="Satoshi", fontSize=7.2, leading=10, textColor=MUTED),
        "table_head": ParagraphStyle("TableHead", parent=sheet["Normal"], fontName="Satoshi-Bold", fontSize=6.8, leading=8, textColor=MUTED),
        "table": ParagraphStyle("Table", parent=sheet["Normal"], fontName="Satoshi", fontSize=7.4, leading=9, textColor=INK),
        "table_bold": ParagraphStyle("TableBold", parent=sheet["Normal"], fontName="Satoshi-Bold", fontSize=7.4, leading=9, textColor=INK),
        "right": ParagraphStyle("Right", parent=sheet["Normal"], fontName="Satoshi", fontSize=7.4, leading=9, textColor=INK, alignment=TA_RIGHT),
    }


def page_frame(canvas, doc):
    canvas.saveState()
    width, height = A4
    canvas.drawImage(str(LEMON_LOGO), 20 * mm, height - 16.2 * mm, width=29 * mm, height=6.6 * mm, preserveAspectRatio=True, mask="auto")
    canvas.setFillColor(MUTED)
    canvas.setFont("Satoshi-Bold", 6.8)
    canvas.drawRightString(width - 20 * mm, height - 13.2 * mm, doc.title.upper())
    canvas.setStrokeColor(LINE)
    canvas.line(20 * mm, height - 19 * mm, width - 20 * mm, height - 19 * mm)
    canvas.setStrokeColor(LINE)
    canvas.line(20 * mm, 15 * mm, width - 20 * mm, 15 * mm)
    canvas.setFillColor(MUTED)
    canvas.setFont("Satoshi", 7)
    canvas.drawString(20 * mm, 9.5 * mm, "FERCED x LEMON - 27.08.2026")
    canvas.drawRightString(width - 20 * mm, 9.5 * mm, f"Página {doc.page}")
    canvas.restoreState()


def document(path, title):
    return SimpleDocTemplate(
        str(path),
        pagesize=A4,
        rightMargin=20 * mm,
        leftMargin=20 * mm,
        topMargin=29 * mm,
        bottomMargin=20 * mm,
        title=title,
        author="Ferced",
    )


def paragraph(text, style):
    return Paragraph(text, style)


def pricing_table(style_set):
    rows = [
        ["Volumen", "Unitario neto", "Unitario con IVA", "Total neto", "Total con IVA"],
        ['100 unidades<br/><font color="#009B1A">34,2% menos</font>', "USDC 79", "USDC 95,59", "USDC 7.900", "USDC 9.559"],
        ['150 unidades<br/><font color="#009B1A">37,5% menos</font>', "USDC 75", "USDC 90,75", "USDC 11.250", "USDC 13.612,50"],
        ['200 unidades<br/><font color="#009B1A">41,7% menos</font>', "USDC 70", "USDC 84,70", "USDC 14.000", "USDC 16.940"],
    ]
    return styled_table(rows, [42, 31, 35, 31, 35], style_set)


def total_table(style_set):
    rows = [
        ["Lote", "Hardware neto", "Hardware con IVA", "Con software neto", "Con software e IVA"],
        ["100 unidades", "7.900", "9.559", "12.400", "15.004"],
        ["150 unidades", "11.250", "13.612,50", "15.750", "19.057,50"],
        ["200 unidades", "14.000", "16.940", "18.500", "22.385"],
    ]
    return styled_table(rows, [38, 34, 36, 34, 38], style_set)


def styled_table(rows, widths_mm, style_set):
    rendered = []
    for row_index, row in enumerate(rows):
        rendered.append([
            paragraph(cell, style_set["table_head"] if row_index == 0 else style_set["table_bold"] if col_index == 0 else style_set["right"])
            for col_index, cell in enumerate(row)
        ])
    table = Table(rendered, colWidths=[value * mm for value in widths_mm], repeatRows=1, hAlign="LEFT")
    table.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), PALE),
        ("LINEBELOW", (0, 0), (-1, -1), 0.5, LINE),
        ("VALIGN", (0, 0), (-1, -1), "MIDDLE"),
        ("LEFTPADDING", (0, 0), (-1, -1), 7),
        ("RIGHTPADDING", (0, 0), (-1, -1), 7),
        ("TOPPADDING", (0, 0), (-1, -1), 9),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 9),
    ]))
    return table


def option_table(style_set):
    content = [
        [paragraph("INCLUIDO CON EL LOTE", style_set["kicker"]), paragraph("SOFTWARE OPCIONAL", style_set["kicker"])],
        [paragraph("Programa de firmware - 60 días", style_set["h2"]), paragraph("USDC 4.500 netos", style_set["h2"])],
        [paragraph("Entrega inicial con el firmware Stocks, desarrollo conjunto del firmware principal y actualización coordinada de todas las unidades a la versión aprobada.", style_set["body"]), paragraph("Control de flota + Developer Kit: panel interno, gestión de versiones, firmware base, SDK, plantillas, simulador, compilación y Skill de desarrollo.", style_set["body"])],
    ]
    table = Table(content, colWidths=[84 * mm, 84 * mm])
    table.setStyle(TableStyle([
        ("BOX", (0, 0), (-1, -1), 0.6, LINE),
        ("INNERGRID", (0, 0), (-1, -1), 0.6, LINE),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
        ("LEFTPADDING", (0, 0), (-1, -1), 13),
        ("RIGHTPADDING", (0, 0), (-1, -1), 13),
        ("TOPPADDING", (0, 0), (-1, -1), 8),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 8),
    ]))
    return table


def build_commercial(path, style_set):
    story = [
        Spacer(1, 12 * mm),
        paragraph("PROPUESTA COMERCIAL", style_set["kicker"]),
        paragraph("Fabricación y evolución de Lemon Box", style_set["title"]),
        paragraph("Propuesta para producir un nuevo lote, entregar una experiencia funcional desde el inicio y desarrollar junto a Lemon el firmware principal del producto.", style_set["subtitle"]),
        paragraph("Preparada para Lemon | Versión 27.08.2026 | Valores en USDC", style_set["small"]),
        paragraph("Fabricación", style_set["h2"]),
        pricing_table(style_set),
        Spacer(1, 4 * mm),
        paragraph("Incluye pantalla, carcasa, armado, programación, control de calidad, packaging individual y el programa de firmware. No incluye cables ni fuentes de alimentación. Ahorro calculado contra el último lote adicional de 7 unidades, realizado a USDC 120 por unidad neto.", style_set["body"]),
        paragraph("Alcance incluido y software opcional", style_set["h2"]),
        option_table(style_set),
        Spacer(1, 4 * mm),
        paragraph("El lote completo puede entregarse en hasta 20 días hábiles con el firmware Stocks. El programa incluido contempla 60 días corridos de trabajo conjunto con un equipo de Lemon, la instalación coordinada del firmware final y el recall de hasta 37 unidades ya entregadas. El software es opcional e independiente del lote.", style_set["body"]),
        PageBreak(),
        Spacer(1, 9 * mm),
        paragraph("Inversión total", style_set["h2"]),
        paragraph("Comparación entre la fabricación sola y la fabricación con Control de flota + Developer Kit. Valores expresados en USDC.", style_set["body"]),
        total_table(style_set),
        paragraph("Condiciones", style_set["h2"]),
        paragraph("<b>Pago.</b> 70% al confirmar la producción y 30% contra entrega del lote completo.", style_set["body"]),
        paragraph("<b>Entrega inicial.</b> Hasta 20 días hábiles para el lote completo con el firmware Stocks, contados desde la confirmación, la acreditación del anticipo y la disponibilidad de componentes.", style_set["body"]),
        paragraph("<b>Firmware.</b> 60 días corridos de desarrollo conjunto desde el inicio con el equipo designado por Lemon.", style_set["body"]),
        paragraph("<b>Actualización.</b> Instalación coordinada del firmware final aprobado en todas las unidades del nuevo lote.", style_set["body"]),
        paragraph("<b>Recall incluido.</b> Actualización de carcasa y firmware de hasta 37 Lemon Box ya entregadas. La operatoria se coordina con Lemon antes de iniciar.", style_set["body"]),
        paragraph("<b>Software opcional.</b> Primera versión operativa en 4 a 6 semanas y 90 días de soporte inicial desde la puesta en marcha.", style_set["body"]),
        Spacer(1, 5 * mm),
        KeepTogether([
            Table([[paragraph("El cronograma definitivo se confirma al validar disponibilidad de componentes, terminación del producto y responsables del proyecto.", style_set["body"]) ]], colWidths=[168 * mm], style=TableStyle([("BACKGROUND", (0, 0), (-1, -1), PALE), ("BOX", (0, 0), (-1, -1), .6, LINE), ("LEFTPADDING", (0, 0), (-1, -1), 12), ("RIGHTPADDING", (0, 0), (-1, -1), 12), ("TOPPADDING", (0, 0), (-1, -1), 10), ("BOTTOMPADDING", (0, 0), (-1, -1), 6)]))
        ]),
    ]
    document(path, "Propuesta comercial - Lemon Box").build(story, onFirstPage=page_frame, onLaterPages=page_frame)


def build_platform(path, style_set):
    story = [
        Spacer(1, 12 * mm),
        paragraph("ALCANCE OPERATIVO", style_set["kicker"]),
        paragraph("Control de flota + Developer Kit", style_set["title"]),
        paragraph("Software opcional para operar Lemon Box a escala y permitir que el equipo de Lemon desarrolle nuevas experiencias sobre una base estable.", style_set["subtitle"]),
        paragraph("Preparada para Lemon | Versión 27.08.2026 | Alcance inicial", style_set["small"]),
        paragraph("Objetivo", style_set["h2"]),
        paragraph("Centralizar el inventario y la operación de Lemon Box, reducir el trabajo manual sobre cada dispositivo y dar a los equipos de Lemon herramientas para crear, probar y desplegar nuevos firmwares sin reconstruir la base técnica del producto.", style_set["body"]),
        paragraph("Control de flota", style_set["h2"]),
    ]
    capabilities = [
        ("Dispositivos", "Inventario, identificador, grupo, conexión, versión instalada, configuración y última actividad."),
        ("Grupos", "Organización por equipo, ubicación, campaña, partner o etapa de validación."),
        ("Actualizaciones", "Preparación de versiones, prueba acotada, ampliación progresiva y restauración."),
        ("Actividad", "Registro de publicaciones, configuraciones y acciones administrativas."),
        ("Usuarios", "Acceso por rol y permisos para consultar, preparar o aprobar cambios."),
    ]
    for name, copy in capabilities:
        story.extend([paragraph(name, style_set["h3"]), paragraph(copy, style_set["body"])])
    story.extend([
        PageBreak(),
        Spacer(1, 9 * mm),
        paragraph("Developer Kit", style_set["h2"]),
    ])
    integrations = [
        ("Firmware base", "Funciones comunes de pantalla táctil, Wi-Fi, caché, operación sin conexión, recuperación y actualización."),
        ("SDK y plantillas", "Componentes, ejemplos y contratos de datos para construir firmwares y experiencias sobre una base común."),
        ("Simulador", "Vista previa local para validar contenido, estados y navegación antes de usar hardware real."),
        ("Compilación y validación", "Herramientas para compilar, ejecutar controles técnicos y empaquetar una versión publicable."),
        ("Skill de desarrollo", "Guía para que los asistentes de programación entiendan la arquitectura, generen código compatible y sigan el proceso de pruebas."),
    ]
    story.append(paragraph("<br/>".join(f"<b>{name}.</b> {copy}" for name, copy in integrations), style_set["body"]))
    story.extend([
        Spacer(1, 3 * mm),
        Table([[paragraph("El Developer Kit permite crear y validar versiones. La publicación se realiza desde Control de flota, primero sobre un grupo de prueba y siempre con aprobación explícita.", style_set["body"])]], colWidths=[168 * mm], style=TableStyle([("BACKGROUND", (0, 0), (-1, -1), PALE), ("BOX", (0, 0), (-1, -1), .6, LINE), ("LEFTPADDING", (0, 0), (-1, -1), 12), ("RIGHTPADDING", (0, 0), (-1, -1), 12), ("TOPPADDING", (0, 0), (-1, -1), 10), ("BOTTOMPADDING", (0, 0), (-1, -1), 6)])),
        paragraph("Flujo de trabajo", style_set["h2"]),
        paragraph("1. Partir del firmware principal o crear una variante.<br/>2. Desarrollar con el SDK y las plantillas.<br/>3. Probar en el simulador y sobre hardware real.<br/>4. Validar, compilar y cargar el paquete en Control de flota.<br/>5. Publicar sobre un grupo de prueba, aprobar y ampliar el despliegue.", style_set["body"]),
        paragraph("Entregables iniciales", style_set["h2"]),
        paragraph("1. Aplicación web interna con autenticación y roles básicos.<br/>2. Inventario inicial y grupos de dispositivos.<br/>3. Vista de estado, conexión, versión y actividad.<br/>4. Flujo de prueba y publicación de actualizaciones.<br/>5. Firmware base, SDK de experiencias y plantillas.<br/>6. Simulador, compilación, validación y Skill de desarrollo.<br/>7. Documentación técnica y sesión de puesta en marcha.", style_set["body"]),
        paragraph("Programa incluido con el lote", style_set["h2"]),
        paragraph("La compra de 100, 150 o 200 unidades incluye 60 días corridos de trabajo conjunto y directo con un equipo de Lemon para convertir la versión principal en un firmware estable. Para acelerar la entrega, el lote puede salir inicialmente con el firmware Stocks. Una vez aprobado el firmware final, se coordina su instalación en todas las unidades nuevas.", style_set["body"]),
        paragraph("Plazo y soporte", style_set["h2"]),
        paragraph("El paquete opcional de Control de flota + Developer Kit se estima en 4 a 6 semanas desde el inicio y la validación del alcance. Incluye una sesión de puesta en marcha y 90 días de soporte inicial.", style_set["body"]),
    ])
    document(path, "Control de flota + Developer Kit - Lemon Box").build(story, onFirstPage=page_frame, onLaterPages=page_frame)


def main():
    register_fonts()
    style_set = styles()
    OUTPUT.mkdir(parents=True, exist_ok=True)
    PUBLIC.mkdir(parents=True, exist_ok=True)
    commercial = OUTPUT / "Propuesta-Comercial-Lemon-Box-2026.pdf"
    platform = OUTPUT / "Alcance-Plataforma-Lemon-Box-2026.pdf"
    build_commercial(commercial, style_set)
    build_platform(platform, style_set)
    copyfile(commercial, PUBLIC / "propuesta-comercial.pdf")
    copyfile(platform, PUBLIC / "alcance-plataforma.pdf")
    print(commercial)
    print(platform)


if __name__ == "__main__":
    main()
