package feed

import (
	"testing"
	"time"
)

func TestItemsSortNewestFirst(t *testing.T) {
	base := time.Date(2026, 8, 2, 12, 0, 0, 0, time.UTC)
	items := []Item{
		{ID: "a", Text: "viejo", At: base.Add(-2 * time.Hour)},
		{ID: "b", Text: "nuevo", At: base},
		{ID: "c", Text: "medio", At: base.Add(-1 * time.Hour)},
	}
	Sort(items)
	got := []string{items[0].ID, items[1].ID, items[2].ID}
	want := []string{"b", "c", "a"}
	for i := range want {
		if got[i] != want[i] {
			t.Fatalf("orden = %v, quiero %v", got, want)
		}
	}
}

func TestDedupPrefersFirstSeen(t *testing.T) {
	items := []Item{
		{ID: "x", Text: "primero"},
		{ID: "x", Text: "duplicado"},
		{ID: "y", Text: "otro"},
	}
	out := Dedup(items)
	if len(out) != 2 {
		t.Fatalf("largo = %d, quiero 2", len(out))
	}
	if out[0].Text != "primero" {
		t.Fatalf("texto = %q, quiero %q", out[0].Text, "primero")
	}
}

func TestDedupDoesNotMutateInput(t *testing.T) {
	items := []Item{
		{ID: "x", Text: "primero"},
		{ID: "x", Text: "duplicado"},
		{ID: "y", Text: "otro"},
	}
	Dedup(items)
	if items[1].Text != "duplicado" {
		t.Fatalf("Dedup piso el slice de entrada: %q", items[1].Text)
	}
}

func TestSortIsStableForEqualTimes(t *testing.T) {
	at := time.Date(2026, 8, 2, 12, 0, 0, 0, time.UTC)
	items := []Item{
		{ID: "a", At: at},
		{ID: "b", At: at},
		{ID: "c", At: at},
	}
	Sort(items)
	want := []string{"a", "b", "c"}
	for i := range want {
		if items[i].ID != want[i] {
			t.Fatalf("orden = %v, quiero estable %v", items, want)
		}
	}
}
