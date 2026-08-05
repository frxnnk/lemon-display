package padel

import (
	"fmt"
	"regexp"
	"strconv"
	"strings"

	"github.com/fcedeirajoaquin/ferced-display/proxy/internal/norm"
)

// El orden de juego es una tabla por partido, agrupadas por cancha. La cancha
// vive en un encabezado de seccion aparte, asi que hay que recorrer el
// documento en orden y no tabla por tabla suelta.
var (
	// Las clases se matchean por contenido y no literalmente: el widget escribe
	// "ml-2  line-thin", con dos espacios. Un patron con un espacio solo no
	// engancha nada y el partido se pierde entero y en silencio.
	canchaRe  = regexp.MustCompile(`(?s)<div class="\s*oop-court\s*">(.*?)</div>`)
	tablaRe   = regexp.MustCompile(`(?s)<table class="[^"]*w-100[^"]*">.*?</table>`)
	horaRe    = regexp.MustCompile(`(?s)<span class="[^"]*court-name[^"]*">(.*?)</span>`)
	faseRe    = regexp.MustCompile(`(?s)<div class="[^"]*round-name[^"]*">.*?<b>(.*?)</b>\s*<div>(.*?)</div>`)
	equipoRe  = regexp.MustCompile(`(?s)<td class="[^"]*team[^"]*".*?</td>`)
	jugadorRe = regexp.MustCompile(`(?s)<div class="[^"]*line-thin[^"]*">\s*<span>(.*?)</span>\s*<span[^>]*>(.*?)</span>(?:\s*<small>\((\d+)\)</small>)?`)
	filaRe    = regexp.MustCompile(`(?s)<tr[^>]*>.*?</tr>`)
	setRe     = regexp.MustCompile(`(?s)<td class="[^"]*\bset\b[^"]*">(.*?)</td>`)
	resumenRe = regexp.MustCompile(`(?s)<div class="[^"]*live-status-summary.*?</div>`)

	// El id del widget no se deduce del slug del torneo: hay que leerlo de la
	// pagina, donde padelfip arma la URL de su propio endpoint.
	refRe = regexp.MustCompile(`get-oop-data\.php\?year=(\d+)&(?:amp;)?id=(\d+)&(?:amp;)?day=(\d+)&(?:amp;)?totalday=(\d+)`)

	ampmRe = regexp.MustCompile(`(\d{1,2}):(\d{2})\s*(AM|PM)`)
)

// ParseOOPRef saca de la pagina de un torneo el ano, el id y el total de dias
// que necesita el widget de orden de juego.
func ParseOOPRef(html string) (oopRef, error) {
	m := refRe.FindStringSubmatch(html)
	if m == nil {
		return oopRef{}, fmt.Errorf("la pagina del torneo no trae get-oop-data")
	}
	year, _ := strconv.Atoi(m[1])
	day, _ := strconv.Atoi(m[3])
	total, _ := strconv.Atoi(m[4])
	if year == 0 || m[2] == "" {
		return oopRef{}, fmt.Errorf("get-oop-data con parametros vacios")
	}
	if day <= 0 {
		day = 1
	}
	return oopRef{year: year, id: m[2], day: day, totalDay: total}, nil
}

// hora24 pasa "Starting at 10:00 AM" a "10:00" y "Not before 3:30 PM" a
// "no antes de 15:30". El resto de las etiquetas se traducen enteras: en la
// pantalla no hay lugar para una frase y el ingles desentona con el resto.
func hora24(s string) string {
	s = strings.TrimSpace(norm.Clean(norm.StripHTML(s)))
	if s == "" {
		return ""
	}

	convertida := ""
	if m := ampmRe.FindStringSubmatch(s); m != nil {
		h, _ := strconv.Atoi(m[1])
		if strings.EqualFold(m[3], "PM") && h != 12 {
			h += 12
		}
		if strings.EqualFold(m[3], "AM") && h == 12 {
			h = 0
		}
		convertida = fmt.Sprintf("%02d:%s", h, m[2])
	}

	low := strings.ToLower(s)
	switch {
	case strings.HasPrefix(low, "starting at") && convertida != "":
		return convertida
	case strings.HasPrefix(low, "not before") && convertida != "":
		return "no antes de " + convertida
	case strings.HasPrefix(low, "followed by"):
		return "a seguir"
	case strings.HasPrefix(low, "after"):
		return "a seguir"
	case convertida != "":
		return convertida
	}
	return s
}

// fase acorta los nombres de ronda. En 480 px de ancho "Round of 32" compite
// con el nombre del torneo por el mismo renglon.
func fase(s string) string {
	s = strings.TrimSpace(norm.Clean(norm.StripHTML(s)))
	low := strings.ToLower(s)
	switch {
	case strings.Contains(low, "round of 128"):
		return "R128"
	case strings.Contains(low, "round of 64"):
		return "R64"
	case strings.Contains(low, "round of 32"):
		return "R32"
	case strings.Contains(low, "round of 16"):
		return "Octavos"
	case strings.Contains(low, "quarter"):
		return "Cuartos"
	case strings.Contains(low, "semi"):
		return "Semis"
	case low == "final" || strings.HasPrefix(low, "final"):
		return "FINAL"
	}
	return s
}

func genero(s string) string {
	low := strings.ToLower(strings.TrimSpace(norm.StripHTML(s)))
	switch {
	case strings.HasPrefix(low, "women"):
		return "F"
	case strings.HasPrefix(low, "men"):
		return "M"
	}
	return ""
}

type equipo struct {
	p1, p2 string
	seed   string
	sets   []string
}

func parseEquipo(fila string) equipo {
	var e equipo

	for _, m := range jugadorRe.FindAllStringSubmatch(fila, -1) {
		inicial := strings.TrimSpace(norm.Clean(norm.StripHTML(m[1])))
		apellido := strings.TrimSpace(norm.Clean(norm.StripHTML(m[2])))
		nombre := strings.TrimSpace(inicial + " " + apellido)
		if nombre == "" {
			continue
		}
		if m[3] != "" {
			e.seed = m[3]
		}
		if e.p1 == "" {
			e.p1 = nombre
		} else if e.p2 == "" {
			e.p2 = nombre
		}
	}

	// Los sets vacios llegan como "-": no son un cero, son un partido que
	// todavia no se jugo, y mostrarlos seria mentir.
	for _, m := range setRe.FindAllStringSubmatch(fila, -1) {
		v := strings.TrimSpace(norm.StripHTML(m[1]))
		if v == "" || v == "-" {
			continue
		}
		e.sets = append(e.sets, v)
	}
	return e
}

// ParseOOP extrae los partidos del widget de orden de juego de un dia.
func ParseOOP(html string) []Match {
	// Indices de los encabezados de cancha, para saber a cual pertenece cada
	// tabla: el widget las agrupa por columna, no las anida.
	type marca struct {
		pos    int
		cancha string
	}
	var canchas []marca
	for _, loc := range canchaRe.FindAllStringSubmatchIndex(html, -1) {
		canchas = append(canchas, marca{
			pos:    loc[0],
			cancha: strings.TrimSpace(norm.Clean(norm.StripHTML(html[loc[2]:loc[3]]))),
		})
	}

	canchaDe := func(pos int) string {
		out := ""
		for _, c := range canchas {
			if c.pos < pos {
				out = c.cancha
			}
		}
		return out
	}

	var out []Match
	for _, loc := range tablaRe.FindAllStringIndex(html, -1) {
		tabla := html[loc[0]:loc[1]]

		filas := filaRe.FindAllString(tabla, -1)
		equipos := equipoRe.FindAllString(tabla, -1)
		if len(equipos) < 2 || len(filas) < 3 {
			continue
		}

		// Los sets viven en la <tr> del equipo, no dentro del <td class="team">,
		// asi que el equipo se arma con la fila entera.
		var conSets []string
		for _, f := range filas {
			// Con equipoRe y no con un Contains del literal: la clase puede
			// venir con espacios de mas, que es justo lo que rompio el patron
			// de los jugadores.
			if equipoRe.MatchString(f) {
				conSets = append(conSets, f)
			}
		}
		if len(conSets) < 2 {
			continue
		}
		a := parseEquipo(conSets[0])
		b := parseEquipo(conSets[1])
		if a.p1 == "" || b.p1 == "" {
			continue
		}

		m := Match{
			Court: canchaDe(loc[0]),
			A1:    a.p1, A2: a.p2, B1: b.p1, B2: b.p2,
			SeedA: a.seed, SeedB: b.seed,
			ScoreA: strings.Join(a.sets, " "),
			ScoreB: strings.Join(b.sets, " "),
		}

		if hm := horaRe.FindStringSubmatch(tabla); hm != nil {
			m.Time = hora24(hm[1])
		}
		if fm := faseRe.FindStringSubmatch(tabla); fm != nil {
			m.Gender = genero(fm[1])
			m.Round = fase(fm[2])
		}

		// El resumen trae el estado y la duracion; "MATCH STATS" es el rotulo
		// del boton del modal y no dice nada del partido.
		if rm := resumenRe.FindString(tabla); rm != "" {
			txt := norm.Clean(norm.StripHTML(rm))
			txt = strings.TrimSpace(strings.ReplaceAll(txt, "MATCH STATS", ""))
			low := strings.ToLower(txt)
			switch {
			case strings.Contains(low, "completed"), strings.Contains(low, "finished"):
				m.State = "listo"
			case strings.Contains(low, "live"), strings.Contains(low, "playing"),
				strings.Contains(low, "in progress"):
				m.State = "jugando"
			}
		}
		// Un partido con sets cargados y sin rotulo de estado igual se jugo: el
		// widget no siempre escribe "Completed".
		if m.State == "" && (m.ScoreA != "" || m.ScoreB != "") {
			m.State = "jugando"
		}

		out = append(out, m)
	}
	return out
}
