package sorsa

import (
	"os"
	"testing"

	"github.com/fcedeirajoaquin/ferced-display/proxy/internal/feed"
)

// schema_example.json esta armado a mano desde el swagger oficial
// (common.TweetsResponse). Corre siempre: valida el parser contra el shape
// documentado.
const schemaFixture = "testdata/schema_example.json"

// list_tweets.json es una captura REAL de la API, tomada el 2026-08-03 contra
// /v3/search-tweets y no contra /list-tweets, porque la Lista publica todavia
// no existe. Vale igual: los dos endpoints devuelven common.TweetsResponse, que
// es exactamente lo que este fixture valida — que el shape documentado coincida
// con el que la API devuelve de verdad.
const realFixture = "testdata/list_tweets.json"

func parseFile(t *testing.T, path string) []feed.Item {
	t.Helper()
	raw, err := os.ReadFile(path)
	if err != nil {
		t.Skipf("falta %s", path)
	}
	items, err := parse(raw)
	if err != nil {
		t.Fatalf("parse fallo: %v", err)
	}
	return items
}

func TestParsesDocumentedSchema(t *testing.T) {
	items := parseFile(t, schemaFixture)
	if len(items) == 0 {
		t.Fatal("no parseo ningun tweet")
	}
}

func TestSkipsRepliesAndEmptyText(t *testing.T) {
	items := parseFile(t, schemaFixture)
	// El fixture tiene 4 tweets: uno es respuesta y otro tiene texto vacio.
	if len(items) != 2 {
		t.Fatalf("items = %d, quiero 2 (descarta la respuesta y el vacio)", len(items))
	}
	for _, it := range items {
		if it.Handle == "@respondedor" {
			t.Error("no deberia entrar una respuesta")
		}
	}
}

func TestMapsFullTextAndUser(t *testing.T) {
	items := parseFile(t, schemaFixture)
	first := items[0]

	if first.Handle != "@ferced" {
		t.Errorf("handle = %q, quiero @ferced", first.Handle)
	}
	if first.Author != "Ferced" {
		t.Errorf("autor = %q", first.Author)
	}
	if first.ID != "x:1782368585664626774" {
		t.Errorf("id = %q", first.ID)
	}
	if first.From != feed.OriginX {
		t.Errorf("origen = %q", first.From)
	}
}

func TestNormalizesTextAndCollapsesURL(t *testing.T) {
	items := parseFile(t, schemaFixture)

	if want := "Publicamos la nueva version del firmware - mas rapida y con soporte de temas [link]"; items[0].Text != want {
		t.Errorf("texto  = %q\nquiero = %q", items[0].Text, want)
	}
	if items[1].Text != "Buenisimo esto" {
		t.Errorf("emoji no removido: %q", items[1].Text)
	}
	if items[1].Author != "Alguien Nandu" {
		t.Errorf("autor sin normalizar: %q", items[1].Author)
	}
}

func TestTextIsASCIIAndBounded(t *testing.T) {
	for _, it := range parseFile(t, schemaFixture) {
		for i := 0; i < len(it.Text); i++ {
			if it.Text[i] > 127 {
				t.Fatalf("texto no normalizado: %q", it.Text)
			}
		}
		if len(it.Text) > maxText+1 {
			t.Errorf("texto sin recortar: %d bytes", len(it.Text))
		}
	}
}

func TestDatesParsed(t *testing.T) {
	for _, it := range parseFile(t, schemaFixture) {
		if it.Epoch <= 0 {
			t.Fatalf("fecha sin parsear (%q): revisar dateLayouts", it.Text)
		}
	}
}

func TestSortedNewestFirst(t *testing.T) {
	items := parseFile(t, schemaFixture)
	if !items[0].At.After(items[1].At) {
		t.Error("el primero deberia ser el mas nuevo")
	}
}

func TestParseRejectsGarbage(t *testing.T) {
	if _, err := parse([]byte("no soy json")); err == nil {
		t.Error("quiero error con JSON invalido")
	}
}

// Confirma contra la API real. Skip hasta que se capture el fixture.
func TestRealAPIMatchesDocumentedSchema(t *testing.T) {
	items := parseFile(t, realFixture)
	if len(items) == 0 {
		t.Fatal("la captura real no parseo nada: el shape documentado no coincide con la API")
	}
	for _, it := range items {
		if it.ID == "x:" {
			t.Error("tweet sin id en la respuesta real")
		}
		if it.Handle == "@" {
			t.Error("tweet sin username en la respuesta real")
		}
		if it.Epoch <= 0 {
			t.Errorf("fecha real sin parsear en %q", it.Text)
		}
	}
}
