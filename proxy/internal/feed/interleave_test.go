package feed

import (
	"testing"
	"time"
)

func mk(id string, o Origin, h int) Item {
	return Item{
		ID:   id,
		From: o,
		At:   time.Date(2026, 8, 2, h, 0, 0, 0, time.UTC),
	}
}

func origins(items []Item) []Origin {
	out := make([]Origin, len(items))
	for i, it := range items {
		out[i] = it.From
	}
	return out
}

func ids(items []Item) []string {
	out := make([]string, len(items))
	for i, it := range items {
		out[i] = it.ID
	}
	return out
}

func TestInterleaveAlternatesOrigins(t *testing.T) {
	in := []Item{
		mk("t1", OriginTrend, 20), mk("t2", OriginTrend, 19), mk("t3", OriginTrend, 18),
		mk("r1", OriginRSS, 17), mk("r2", OriginRSS, 16), mk("r3", OriginRSS, 15),
	}
	got := origins(Interleave(in))
	want := []Origin{OriginTrend, OriginRSS, OriginTrend, OriginRSS, OriginTrend, OriginRSS}
	for i := range want {
		if got[i] != want[i] {
			t.Fatalf("origenes = %v, quiero %v", got, want)
		}
	}
}

func TestInterleavePreservesOrderWithinOrigin(t *testing.T) {
	in := []Item{
		mk("t1", OriginTrend, 20), mk("t2", OriginTrend, 19),
		mk("r1", OriginRSS, 18), mk("r2", OriginRSS, 17),
	}
	got := ids(Interleave(in))
	want := []string{"t1", "r1", "t2", "r2"}
	for i := range want {
		if got[i] != want[i] {
			t.Fatalf("ids = %v, quiero %v", got, want)
		}
	}
}

func TestInterleaveHandlesUnevenGroups(t *testing.T) {
	in := []Item{
		mk("t1", OriginTrend, 20),
		mk("r1", OriginRSS, 19), mk("r2", OriginRSS, 18), mk("r3", OriginRSS, 17),
	}
	got := ids(Interleave(in))
	want := []string{"t1", "r1", "r2", "r3"}
	if len(got) != len(want) {
		t.Fatalf("largo = %d, quiero %d (%v)", len(got), len(want), got)
	}
	for i := range want {
		if got[i] != want[i] {
			t.Fatalf("ids = %v, quiero %v", got, want)
		}
	}
}

func TestInterleaveKeepsEveryItem(t *testing.T) {
	in := []Item{
		mk("t1", OriginTrend, 20), mk("t2", OriginTrend, 19),
		mk("r1", OriginRSS, 18),
		mk("x1", OriginX, 17), mk("x2", OriginX, 16), mk("x3", OriginX, 15),
	}
	got := Interleave(in)
	if len(got) != len(in) {
		t.Fatalf("largo = %d, quiero %d", len(got), len(in))
	}
	seen := map[string]bool{}
	for _, it := range got {
		if seen[it.ID] {
			t.Fatalf("item duplicado: %q", it.ID)
		}
		seen[it.ID] = true
	}
}

func TestInterleaveGroupOrderFollowsFirstAppearance(t *testing.T) {
	in := []Item{
		mk("r1", OriginRSS, 20),
		mk("t1", OriginTrend, 19),
		mk("r2", OriginRSS, 18),
	}
	got := origins(Interleave(in))
	if got[0] != OriginRSS || got[1] != OriginTrend {
		t.Fatalf("origenes = %v, quiero que respete el primer avistamiento", got)
	}
}

func TestInterleaveSingleOriginIsUnchanged(t *testing.T) {
	in := []Item{mk("a", OriginRSS, 20), mk("b", OriginRSS, 19), mk("c", OriginRSS, 18)}
	got := ids(Interleave(in))
	want := []string{"a", "b", "c"}
	for i := range want {
		if got[i] != want[i] {
			t.Fatalf("ids = %v, quiero %v", got, want)
		}
	}
}

func TestInterleaveEmptyIsEmpty(t *testing.T) {
	if got := Interleave(nil); len(got) != 0 {
		t.Fatalf("quiero vacio, tengo %v", got)
	}
}
