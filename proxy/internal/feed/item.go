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
