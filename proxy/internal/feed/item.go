package feed

import (
	"sort"
	"time"
)

type Origin string

const (
	OriginX     Origin = "x"
	OriginRSS   Origin = "rss"
	OriginTrend Origin = "trend"
)

type Item struct {
	ID     string    `json:"id"`
	Text   string    `json:"t"`
	Author string    `json:"a"`
	Handle string    `json:"h"`
	At     time.Time `json:"-"`
	Epoch  int64     `json:"ts"`
	From   Origin    `json:"o"`

	// ImgURL es la fuente original; nunca sale al firmware. El proxy la
	// resuelve y publica solo ImgKey, asi el aparato no puede pedir una URL
	// arbitraria.
	ImgURL string `json:"-"`
	ImgKey string `json:"k,omitempty"`

	// Src identifica la fuente concreta ("rss:BBC Mundo"), no su tipo. El
	// intercalado agrupa por esto: si agrupara por Origin, cuatro feeds RSS
	// contarian como uno solo y el mas nuevo coparia la rotacion.
	Src string `json:"-"`
}

func Sort(items []Item) {
	sort.SliceStable(items, func(i, j int) bool {
		return items[i].At.After(items[j].At)
	})
}

func Dedup(items []Item) []Item {
	seen := make(map[string]struct{}, len(items))
	out := make([]Item, 0, len(items))
	for _, it := range items {
		if _, dup := seen[it.ID]; dup {
			continue
		}
		seen[it.ID] = struct{}{}
		out = append(out, it)
	}
	return out
}

type Source interface {
	Name() string
	Fetch(n int) ([]Item, error)
}
