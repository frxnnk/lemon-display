package mixer

import (
	"log"
	"sync"
	"time"

	"github.com/fcedeirajoaquin/ferced-display/proxy/internal/feed"
)

type Mixer struct {
	ttl     time.Duration
	sources []feed.Source

	mu      sync.Mutex
	pool    []feed.Item
	fetched time.Time
	hasPool bool
}

func New(ttl time.Duration, sources ...feed.Source) *Mixer {
	return &Mixer{ttl: ttl, sources: sources}
}

// Feed devuelve hasta n items. Si una fuente falla sigue con las otras; si
// fallan todas, sirve el ultimo pool bueno en vez de dejar la pantalla vacia.
func (m *Mixer) Feed(n int) []feed.Item {
	m.mu.Lock()
	defer m.mu.Unlock()

	if m.hasPool && time.Since(m.fetched) < m.ttl {
		return m.take(n)
	}

	var merged []feed.Item
	ok := false
	for _, s := range m.sources {
		items, err := s.Fetch(n)
		if err != nil {
			log.Printf("[mixer] %s fallo: %v", s.Name(), err)
			continue
		}
		ok = true
		merged = append(merged, items...)
	}

	if !ok {
		log.Print("[mixer] todas las fuentes fallaron; sirvo el pool anterior")
		return m.take(n)
	}

	feed.Sort(merged)
	m.pool = feed.Dedup(merged)
	m.fetched = time.Now()
	m.hasPool = true
	return m.take(n)
}

func (m *Mixer) take(n int) []feed.Item {
	if n > len(m.pool) {
		n = len(m.pool)
	}
	if n < 0 {
		n = 0
	}
	out := make([]feed.Item, n)
	copy(out, m.pool[:n])
	return out
}
