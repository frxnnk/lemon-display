package mixer

import (
	"errors"
	"testing"
	"time"

	"github.com/fcedeirajoaquin/ferced-display/proxy/internal/feed"
)

type fake struct {
	name  string
	items []feed.Item
	err   error
	calls int
}

func (f *fake) Name() string { return f.name }

func (f *fake) Fetch(n int) ([]feed.Item, error) {
	f.calls++
	if f.err != nil {
		return nil, f.err
	}
	return f.items, nil
}

func at(h int) time.Time {
	return time.Date(2026, 8, 2, h, 0, 0, 0, time.UTC)
}

func TestMergesBothSourcesNewestFirst(t *testing.T) {
	a := &fake{name: "a", items: []feed.Item{{ID: "1", Text: "x", At: at(10)}}}
	b := &fake{name: "b", items: []feed.Item{{ID: "2", Text: "r", At: at(11)}}}
	m := New(2*time.Minute, a, b)

	got := m.Feed(10)
	if len(got) != 2 {
		t.Fatalf("items = %d, quiero 2", len(got))
	}
	if got[0].ID != "2" {
		t.Errorf("primero = %q, quiero el mas nuevo (2)", got[0].ID)
	}
}

func TestOneSourceFailingDoesNotKillFeed(t *testing.T) {
	a := &fake{name: "a", err: errors.New("caida")}
	b := &fake{name: "b", items: []feed.Item{{ID: "2", Text: "r", At: at(11)}}}
	m := New(2*time.Minute, a, b)

	got := m.Feed(10)
	if len(got) != 1 || got[0].ID != "2" {
		t.Fatalf("quiero solo el item de b, tengo %+v", got)
	}
}

func TestBothFailingServesLastGoodPool(t *testing.T) {
	a := &fake{name: "a", items: []feed.Item{{ID: "1", Text: "x", At: at(10)}}}
	m := New(0, a)
	if len(m.Feed(10)) != 1 {
		t.Fatal("primer fetch deberia traer 1")
	}

	a.items, a.err = nil, errors.New("caida")
	got := m.Feed(10)
	if len(got) != 1 || got[0].ID != "1" {
		t.Fatalf("deberia servir el pool viejo, tengo %+v", got)
	}
}

func TestNoPoolAndAllFailingReturnsEmptyNotPanic(t *testing.T) {
	a := &fake{name: "a", err: errors.New("caida")}
	m := New(time.Minute, a)
	if got := m.Feed(10); len(got) != 0 {
		t.Fatalf("quiero vacio, tengo %+v", got)
	}
}

func TestCacheAvoidsRefetch(t *testing.T) {
	a := &fake{name: "a", items: []feed.Item{{ID: "1", Text: "x", At: at(10)}}}
	m := New(time.Hour, a)
	m.Feed(10)
	m.Feed(10)
	if a.calls != 1 {
		t.Fatalf("llamadas = %d, quiero 1 (la segunda sale de cache)", a.calls)
	}
}

func TestDedupsAcrossSources(t *testing.T) {
	a := &fake{name: "a", items: []feed.Item{{ID: "dup", Text: "de a", At: at(11)}}}
	b := &fake{name: "b", items: []feed.Item{{ID: "dup", Text: "de b", At: at(10)}}}
	m := New(time.Minute, a, b)

	got := m.Feed(10)
	if len(got) != 1 {
		t.Fatalf("items = %d, quiero 1 tras dedup", len(got))
	}
}

func TestTakeClampsToPoolSize(t *testing.T) {
	a := &fake{name: "a", items: []feed.Item{
		{ID: "1", At: at(10)},
		{ID: "2", At: at(11)},
	}}
	m := New(time.Minute, a)
	if got := m.Feed(99); len(got) != 2 {
		t.Fatalf("items = %d, quiero 2", len(got))
	}
}

func TestReturnedSliceIsACopy(t *testing.T) {
	a := &fake{name: "a", items: []feed.Item{{ID: "1", Text: "original", At: at(10)}}}
	m := New(time.Hour, a)

	first := m.Feed(10)
	first[0].Text = "pisado"

	second := m.Feed(10)
	if second[0].Text != "original" {
		t.Fatalf("el pool interno se corrompio: %q", second[0].Text)
	}
}

// El boton "Actualizar feed" del aparato disparaba el pedido, pero dentro del
// TTL el mixer devolvia el mismo pool y en pantalla no pasaba nada. Estos tres
// tests fijan el comportamiento que lo arregla.
func TestFeedFreshSalteaElTTL(t *testing.T) {
	a := &fake{name: "a", items: []feed.Item{{ID: "1", Text: "x", At: at(10)}}}
	m := New(10*time.Minute, a)

	m.Feed(10)
	if a.calls != 1 {
		t.Fatalf("llamadas = %d, quiero 1", a.calls)
	}

	// Dentro del TTL, un pedido normal no vuelve a la fuente.
	m.Feed(10)
	if a.calls != 1 {
		t.Fatalf("un pedido normal dentro del TTL no puede refetchear (llamadas = %d)", a.calls)
	}

	// El piso anti-golpeteo se cuenta desde el ultimo fetch: se lo corre para
	// atras en vez de dormir 20 s en un test.
	m.mu.Lock()
	m.fetched = time.Now().Add(-minForzado - time.Second)
	m.mu.Unlock()

	m.FeedFresh(10)
	if a.calls != 2 {
		t.Errorf("FeedFresh tiene que volver a la fuente (llamadas = %d, quiero 2)", a.calls)
	}
}

func TestFeedFreshRespetaElPiso(t *testing.T) {
	a := &fake{name: "a", items: []feed.Item{{ID: "1", Text: "x", At: at(10)}}}
	m := New(10*time.Minute, a)

	m.Feed(10)
	m.FeedFresh(10) // inmediato: cae dentro del piso
	if a.calls != 1 {
		t.Errorf("dos refrescos seguidos no pueden pegarle dos veces a la fuente (llamadas = %d)", a.calls)
	}
}

func TestFeedFreshSinPoolPideIgual(t *testing.T) {
	a := &fake{name: "a", items: []feed.Item{{ID: "1", Text: "x", At: at(10)}}}
	m := New(10*time.Minute, a)

	// Sin pool previo el piso no aplica: si no, el primer refresco a mano
	// devolveria una pantalla vacia.
	if got := m.FeedFresh(10); len(got) != 1 {
		t.Fatalf("items = %d, quiero 1", len(got))
	}
	if a.calls != 1 {
		t.Errorf("llamadas = %d, quiero 1", a.calls)
	}
}
