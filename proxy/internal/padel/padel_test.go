package padel

import (
	"os"
	"strings"
	"testing"
	"time"
)

func leer(t *testing.T, nombre string) string {
	t.Helper()
	b, err := os.ReadFile("testdata/" + nombre)
	if err != nil {
		t.Fatalf("no pude leer %s: %v", nombre, err)
	}
	return string(b)
}

func TestParseCalendar(t *testing.T) {
	ts := ParseCalendar(leer(t, "calendario.html"))
	if len(ts) == 0 {
		t.Fatal("no salio ningun torneo del calendario")
	}

	var london *Tournament
	for i := range ts {
		if strings.Contains(ts[i].Name, "LONDON") {
			london = &ts[i]
		}
	}
	if london == nil {
		t.Fatal("falta LONDON P1, que es el que ancla el resto del test")
	}
	if london.Cat != "P1" {
		t.Errorf("categoria = %q, esperaba P1", london.Cat)
	}
	if london.City != "London" || london.Country != "Great Britain" {
		t.Errorf("sede = %q / %q", london.City, london.Country)
	}
	if london.Slug != "london-p1-2026" {
		t.Errorf("slug = %q", london.Slug)
	}
	if got := london.Start.Format("2006-01-02"); got != "2026-08-02" {
		t.Errorf("inicio = %s", got)
	}
	if got := london.End.Format("2006-01-02"); got != "2026-08-09" {
		t.Errorf("fin = %s", got)
	}
	if london.Rango != "2-9 ago" {
		t.Errorf("rango = %q, esperaba \"2-9 ago\"", london.Rango)
	}

	// Orden por fecha de inicio: la UI muestra "los proximos" y depende de esto.
	for i := 1; i < len(ts); i++ {
		if ts[i].Start.Before(ts[i-1].Start) {
			t.Fatalf("el calendario no quedo ordenado: %s antes que %s",
				ts[i].Name, ts[i-1].Name)
		}
	}
}

// Las categorias que no estan en la tabla (promises, beyond, hexagon) se
// descartan: son ~380 de los 449 torneos del ano y taparian al circuito grande.
func TestParseCalendarDescartaCategoriasDesconocidas(t *testing.T) {
	html := `<article class=" fip-tour-promises-europe">
	  <div class="event-title"><span><a href="https://www.padelfip.com/es/eventos/x-2026/">FIP PROMISES X</a></span></div>
	  <div class="event-time"><div class="date-start-end"> del 02/01/2026 al 04/01/2026 </div></div>
	  <div class="event-location">Alsdorf - Germany</div></article>`
	if ts := ParseCalendar(html); len(ts) != 0 {
		t.Fatalf("esperaba 0 torneos, salieron %d", len(ts))
	}
}

func TestRangoCruzandoDeMes(t *testing.T) {
	a := time.Date(2026, 8, 30, 0, 0, 0, 0, time.UTC)
	b := time.Date(2026, 9, 4, 0, 0, 0, 0, time.UTC)
	if got := rango(a, b); got != "30 ago - 4 sep" {
		t.Errorf("rango = %q", got)
	}
}

func TestParseOOPRef(t *testing.T) {
	ref, err := ParseOOPRef(leer(t, "evento.html"))
	if err != nil {
		t.Fatalf("ParseOOPRef: %v", err)
	}
	if ref.year != 2026 || ref.id != "3208" || ref.totalDay != 8 {
		t.Errorf("ref = %+v", ref)
	}
}

func TestParseOOPRefSinEndpoint(t *testing.T) {
	if _, err := ParseOOPRef("<html>nada</html>"); err == nil {
		t.Fatal("esperaba error cuando la pagina no trae get-oop-data")
	}
}

func TestParseOOP(t *testing.T) {
	ms := ParseOOP(leer(t, "oop.html"))
	if len(ms) == 0 {
		t.Fatal("no salio ningun partido del orden de juego")
	}

	m := ms[0]
	if m.Court != "CENTER COURT" {
		t.Errorf("cancha = %q", m.Court)
	}
	if m.Time != "10:00" {
		t.Errorf("hora = %q, esperaba 10:00 (de \"Starting at 10:00 AM\")", m.Time)
	}
	if m.Round != "R32" {
		t.Errorf("fase = %q", m.Round)
	}
	if m.Gender != "F" {
		t.Errorf("genero = %q", m.Gender)
	}
	if m.A1 != "N. Rodriguez Camacho" || m.A2 != "G. Dal Pozzo" {
		t.Errorf("pareja A = %q / %q", m.A1, m.A2)
	}
	if m.B1 != "M. Ortega Gallego" || m.B2 != "S. Araujo" {
		t.Errorf("pareja B = %q / %q", m.B1, m.B2)
	}
	if m.SeedB != "5" {
		t.Errorf("cabeza de serie B = %q, esperaba 5", m.SeedB)
	}
	// Partido sin jugar: los "-" del widget no son un cero.
	if m.ScoreA != "" || m.ScoreB != "" {
		t.Errorf("un partido sin jugar no puede traer resultado: %q / %q", m.ScoreA, m.ScoreB)
	}
	if m.State != "" {
		t.Errorf("estado = %q, esperaba vacio", m.State)
	}

	// Todas las canchas del fragmento tienen que quedar asignadas: si el
	// recorrido por posicion falla, los partidos de la segunda quedan con la
	// cancha de la primera.
	canchas := map[string]int{}
	for _, x := range ms {
		canchas[x.Court]++
	}
	if len(canchas) < 2 {
		t.Errorf("esperaba partidos en al menos 2 canchas, hubo %v", canchas)
	}
	if canchas[""] > 0 {
		t.Errorf("%d partidos quedaron sin cancha", canchas[""])
	}

	// "Followed by" tiene que traducirse: en la pantalla no va ingles.
	hayASeguir := false
	for _, x := range ms {
		if x.Time == "a seguir" {
			hayASeguir = true
		}
		if strings.Contains(strings.ToLower(x.Time), "followed") ||
			strings.Contains(strings.ToLower(x.Time), "starting") {
			t.Errorf("hora sin traducir: %q", x.Time)
		}
	}
	if !hayASeguir {
		t.Error("esperaba al menos un \"a seguir\" en el fragmento")
	}
}

func TestHora24(t *testing.T) {
	casos := map[string]string{
		"Starting at 10:00 AM": "10:00",
		"Starting at 1:00 PM":  "13:00",
		"Not before 3:30 PM":   "no antes de 15:30",
		"Followed by":          "a seguir",
		"Starting at 12:00 AM": "00:00",
		"Starting at 12:30 PM": "12:30",
	}
	for in, want := range casos {
		if got := hora24(in); got != want {
			t.Errorf("hora24(%q) = %q, esperaba %q", in, got, want)
		}
	}
}

func TestFase(t *testing.T) {
	casos := map[string]string{
		"Round of 64":   "R64",
		"Round of 32":   "R32",
		"Round of 16":   "Octavos",
		"Quarterfinals": "Cuartos",
		"Semifinals":    "Semis",
		"Final":         "FINAL",
		"Q2":            "Q2",
	}
	for in, want := range casos {
		if got := fase(in); got != want {
			t.Errorf("fase(%q) = %q, esperaba %q", in, got, want)
		}
	}
}

func TestRankOrdenaPorJerarquia(t *testing.T) {
	if rank("MAJOR") >= rank("P1") {
		t.Error("un Major tiene que ganarle a un P1")
	}
	if rank("P1") >= rank("GOLD") {
		t.Error("un P1 tiene que ganarle a un Gold")
	}
	if rank("desconocida") <= rank("BRONZE") {
		t.Error("una categoria desconocida tiene que quedar ultima")
	}
}
