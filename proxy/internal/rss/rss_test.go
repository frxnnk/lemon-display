package rss

import (
	"os"
	"strings"
	"testing"

	"github.com/fcedeirajoaquin/ferced-display/proxy/internal/feed"
)

func load(t *testing.T) []feed.Item {
	t.Helper()
	raw, err := os.ReadFile("testdata/sample.xml")
	if err != nil {
		t.Fatal(err)
	}
	items, err := parse(raw, "ejemplo")
	if err != nil {
		t.Fatal(err)
	}
	return items
}

func TestParseSkipsEmptyTitles(t *testing.T) {
	items := load(t)
	if len(items) != 2 {
		t.Fatalf("items = %d, quiero 2 (el tercero tiene titulo vacio)", len(items))
	}
}

func TestParseNormalizesText(t *testing.T) {
	items := load(t)
	if items[0].Text != "Primer titulo con acentuacion" {
		t.Errorf("texto = %q, quiero sin tildes", items[0].Text)
	}
}

func TestParseDecodesEntities(t *testing.T) {
	items := load(t)
	if items[1].Text != "Segundo & con entidad" {
		t.Errorf("entidad mal decodificada: %q", items[1].Text)
	}
}

func TestParseSetsOriginAndIdentity(t *testing.T) {
	items := load(t)
	for _, it := range items {
		if it.From != feed.OriginRSS {
			t.Errorf("origen = %q, quiero rss", it.From)
		}
		if it.ID == "" {
			t.Error("item sin ID")
		}
		if it.Author != "ejemplo" {
			t.Errorf("autor = %q, quiero la etiqueta del feed", it.Author)
		}
	}
}

func TestParseSortsNewestFirst(t *testing.T) {
	items := load(t)
	if !items[0].At.After(items[1].At) {
		t.Error("el primero deberia ser el mas nuevo")
	}
}

func TestParseSetsEpochFromDate(t *testing.T) {
	items := load(t)
	if items[0].Epoch != items[0].At.Unix() {
		t.Errorf("epoch = %d, no coincide con At", items[0].Epoch)
	}
	if items[0].Epoch <= 0 {
		t.Errorf("epoch = %d, no parseo la fecha", items[0].Epoch)
	}
}

// Caso real: BBC Tech titula sus programas "Tech Now" y deja la historia en
// la descripcion. Con solo el titulo la tarjeta queda vacia.
func TestShortTitleUsesDescription(t *testing.T) {
	got := bodyText("Tech Now",
		"New technologies measuring forests and exclusive access to a hacker rehab programme.")
	if len(got) < 40 {
		t.Fatalf("texto = %q, esperaba que sumara la descripcion", got)
	}
	if !strings.HasPrefix(got, "Tech Now.") {
		t.Errorf("el titulo corto deberia encabezar: %q", got)
	}
	if !strings.Contains(got, "forests") {
		t.Errorf("falta el contenido de la descripcion: %q", got)
	}
}

func TestLongTitleIgnoresDescription(t *testing.T) {
	title := "Espana tacha de egoista la respuesta de algunos paises de la UE"
	got := bodyText(title, "Una descripcion larguisima que no deberia aparecer aca para nada.")
	if strings.Contains(got, "descripcion") {
		t.Errorf("no deberia sumar la descripcion: %q", got)
	}
}

func TestDescriptionHTMLIsStripped(t *testing.T) {
	got := bodyText("Corto", "<p>Hola <b>mundo</b> &amp; algo mas para llenar</p>")
	if strings.ContainsAny(got, "<>") {
		t.Errorf("quedo HTML: %q", got)
	}
	if !strings.Contains(got, "Hola mundo & algo") {
		t.Errorf("texto mal decodificado: %q", got)
	}
}

func TestEmptyTitleFallsBackToDescription(t *testing.T) {
	got := bodyText("", "Solo hay descripcion en este item del feed.")
	if !strings.HasPrefix(got, "Solo hay descripcion") {
		t.Errorf("got %q", got)
	}
}

func TestParseRejectsGarbage(t *testing.T) {
	if _, err := parse([]byte("esto no es xml <<<"), "x"); err == nil {
		t.Error("quiero error con XML invalido")
	}
}
