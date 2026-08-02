package rss

import (
	"os"
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

func TestParseRejectsGarbage(t *testing.T) {
	if _, err := parse([]byte("esto no es xml <<<"), "x"); err == nil {
		t.Error("quiero error con XML invalido")
	}
}
