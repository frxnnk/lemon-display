package trends

import (
	"os"
	"testing"
	"time"

	"github.com/fcedeirajoaquin/ferced-display/proxy/internal/feed"
)

// trends_world.json es una captura REAL de /trends?woeid=1 (2026-08-02).
// Incluye tendencias en arabe a proposito: al transliterar quedan como
// simbolos sueltos y hay que descartarlas, no mostrarlas vacias.
const fixture = "testdata/trends_world.json"

var at = time.Date(2026, 8, 2, 18, 0, 0, 0, time.UTC)

func load(t *testing.T) []feed.Item {
	t.Helper()
	raw, err := os.ReadFile(fixture)
	if err != nil {
		t.Fatal(err)
	}
	items, err := parse(raw, "Mundo", at)
	if err != nil {
		t.Fatal(err)
	}
	return items
}

func TestDropsTrendsWithoutASCIIContent(t *testing.T) {
	items := load(t)
	// El fixture trae 12; dos son en arabe y no sobreviven la transliteracion.
	if len(items) != 10 {
		got := make([]string, len(items))
		for i, it := range items {
			got[i] = it.Text
		}
		t.Fatalf("items = %d, quiero 10. tengo: %q", len(items), got)
	}
}

func TestNoItemIsVisualGarbage(t *testing.T) {
	for _, it := range load(t) {
		alnum := 0
		for i := 0; i < len(it.Text); i++ {
			c := it.Text[i]
			if (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') {
				alnum++
			}
		}
		if alnum < 2 {
			t.Errorf("texto sin contenido legible: %q", it.Text)
		}
	}
}

func TestKeepsHashtagsAndNames(t *testing.T) {
	items := load(t)
	seen := map[string]bool{}
	for _, it := range items {
		seen[it.Text] = true
	}
	for _, want := range []string{"#weloveyouariana", "#daha17", "Gorosito", "Tim Payne"} {
		if !seen[want] {
			t.Errorf("falta la tendencia %q", want)
		}
	}
}

func TestSetsOriginAndRegion(t *testing.T) {
	for _, it := range load(t) {
		if it.From != feed.OriginTrend {
			t.Errorf("origen = %q, quiero trend", it.From)
		}
		if it.Author != "Mundo" {
			t.Errorf("region = %q, quiero Mundo", it.Author)
		}
		if it.Epoch != at.Unix() {
			t.Errorf("epoch = %d, quiero el de la consulta", it.Epoch)
		}
	}
}

func TestIDsAreUniqueAndNamespaced(t *testing.T) {
	seen := map[string]bool{}
	for _, it := range load(t) {
		if seen[it.ID] {
			t.Errorf("ID duplicado: %q", it.ID)
		}
		seen[it.ID] = true
		if len(it.ID) < 6 || it.ID[:6] != "trend:" {
			t.Errorf("ID sin namespace: %q", it.ID)
		}
	}
}

// El mismo tema puede ser tendencia en varias regiones. El ID no lleva region
// para que el dedup del mezclador lo colapse en vez de mostrarlo dos veces.
func TestIDIsRegionIndependent(t *testing.T) {
	raw, err := os.ReadFile(fixture)
	if err != nil {
		t.Fatal(err)
	}
	ar, err := parse(raw, "Argentina", at)
	if err != nil {
		t.Fatal(err)
	}
	mundo, err := parse(raw, "Mundo", at)
	if err != nil {
		t.Fatal(err)
	}
	if ar[0].ID != mundo[0].ID {
		t.Fatalf("el mismo tema da IDs distintos: %q vs %q", ar[0].ID, mundo[0].ID)
	}
	if ar[0].Author == mundo[0].Author {
		t.Error("la region deberia seguir diferenciandose en Author")
	}
}

func TestTextIsASCII(t *testing.T) {
	for _, it := range load(t) {
		for i := 0; i < len(it.Text); i++ {
			if it.Text[i] > 127 {
				t.Fatalf("byte no-ASCII en %q", it.Text)
			}
		}
	}
}

func TestParseRejectsGarbage(t *testing.T) {
	if _, err := parse([]byte("no soy json"), "Mundo", at); err == nil {
		t.Error("quiero error con JSON invalido")
	}
}
