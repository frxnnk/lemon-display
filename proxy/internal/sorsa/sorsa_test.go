package sorsa

import (
	"os"
	"testing"

	"github.com/fcedeirajoaquin/ferced-display/proxy/internal/feed"
)

const fixture = "testdata/list_tweets.json"

// Este test valida el contrato contra una respuesta REAL de Sorsa. Sin el
// fixture queda en skip: el shape de rawResp esta asumido, no verificado.
func loadFixture(t *testing.T) []feed.Item {
	t.Helper()
	raw, err := os.ReadFile(fixture)
	if err != nil {
		t.Skipf("falta %s; capturalo con curl (ver el plan, prerequisito T0)", fixture)
	}
	items, err := parse(raw)
	if err != nil {
		t.Fatalf("parse fallo: %v", err)
	}
	return items
}

func TestParsesSomething(t *testing.T) {
	if items := loadFixture(t); len(items) == 0 {
		t.Fatal("no parseo ningun tweet: el shape de rawResp no coincide con la API")
	}
}

func TestItemsAreComplete(t *testing.T) {
	for _, it := range loadFixture(t) {
		if it.ID == "" || it.ID == "x:" {
			t.Error("item sin ID")
		}
		if it.Text == "" {
			t.Error("item sin texto")
		}
		if it.Handle == "" || it.Handle == "@" {
			t.Error("item sin handle")
		}
		if it.From != feed.OriginX {
			t.Errorf("origen = %q, quiero x", it.From)
		}
	}
}

func TestTextIsASCIIAndBounded(t *testing.T) {
	for _, it := range loadFixture(t) {
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
	for _, it := range loadFixture(t) {
		if it.Epoch <= 0 {
			t.Fatalf("fecha sin parsear (%q): revisar dateLayouts", it.Text)
		}
	}
}

func TestParseRejectsGarbage(t *testing.T) {
	if _, err := parse([]byte("no soy json")); err == nil {
		t.Error("quiero error con JSON invalido")
	}
}
