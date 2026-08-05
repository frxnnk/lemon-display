package padel

import (
	"fmt"
	"regexp"
	"sort"
	"strings"
	"time"

	"github.com/fcedeirajoaquin/ferced-display/proxy/internal/norm"
)

// El calendario de padelfip.com viene renderizado del lado del servidor: un
// <article> por torneo, con la categoria en la clase del articulo y el resto en
// divs con clase propia. No hace falta ejecutar JavaScript ni pedir una API.
var (
	articuloRe = regexp.MustCompile(`(?s)<article.*?</article>`)
	claseRe    = regexp.MustCompile(`<article class="\s*([^"]*)"`)
	tituloRe   = regexp.MustCompile(`(?s)<div class="event-title">.*?<a[^>]*>(.*?)</a>`)
	fechasRe   = regexp.MustCompile(`del\s+(\d{2}/\d{2}/\d{4})\s+al\s+(\d{2}/\d{2}/\d{4})`)
	lugarRe    = regexp.MustCompile(`(?s)<div class="event-location">(.*?)</div>`)
	slugRe     = regexp.MustCompile(`https://www\.padelfip\.com/(?:[a-z]{2}/)?eventos/([a-z0-9\-]+)/`)
)

var meses = [...]string{
	"ene", "feb", "mar", "abr", "may", "jun",
	"jul", "ago", "sep", "oct", "nov", "dic",
}

// rango formatea "2-9 ago" cuando el torneo no cruza de mes, y
// "30 ago - 4 sep" cuando si. El aparato no hace aritmetica de calendario.
func rango(a, b time.Time) string {
	ma := meses[int(a.Month())-1]
	mb := meses[int(b.Month())-1]
	if ma == mb {
		return fmt.Sprintf("%d-%d %s", a.Day(), b.Day(), ma)
	}
	return fmt.Sprintf("%d %s - %d %s", a.Day(), ma, b.Day(), mb)
}

// ParseCalendar extrae los torneos de la pagina del calendario anual.
// Devuelve todo lo que encuentra, ordenado por fecha de inicio: filtrar por
// categoria es decision de quien llama.
func ParseCalendar(html string) []Tournament {
	var out []Tournament

	for _, art := range articuloRe.FindAllString(html, -1) {
		cm := claseRe.FindStringSubmatch(art)
		tm := tituloRe.FindStringSubmatch(art)
		fm := fechasRe.FindStringSubmatch(art)
		if tm == nil || fm == nil {
			continue
		}

		cat := ""
		if cm != nil {
			cat = labelDeSlug(cm[1])
		}
		if cat == "" {
			continue // categoria que no esta en la tabla: promises, beyond, etc.
		}

		desde, err := time.Parse("02/01/2006", fm[1])
		if err != nil {
			continue
		}
		hasta, err := time.Parse("02/01/2006", fm[2])
		if err != nil {
			continue
		}

		t := Tournament{
			Cat:   cat,
			Name:  norm.Clean(norm.StripHTML(tm[1])),
			Start: desde,
			End:   hasta,
			Rango: rango(desde, hasta),
		}

		if sm := slugRe.FindStringSubmatch(art); sm != nil {
			t.Slug = sm[1]
		}

		// "Melbourne - Australia". Se parte en ciudad y pais porque en pantalla
		// entran en renglones distintos y con jerarquia distinta.
		if lm := lugarRe.FindStringSubmatch(art); lm != nil {
			lugar := norm.Clean(norm.StripHTML(lm[1]))
			if i := strings.LastIndex(lugar, " - "); i > 0 {
				t.City, t.Country = strings.TrimSpace(lugar[:i]), strings.TrimSpace(lugar[i+3:])
			} else {
				t.City = lugar
			}
		}

		if t.Name == "" {
			continue
		}
		out = append(out, t)
	}

	sort.SliceStable(out, func(i, j int) bool { return out[i].Start.Before(out[j].Start) })
	return out
}
