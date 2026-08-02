package trends

import (
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"time"

	"github.com/fcedeirajoaquin/ferced-display/proxy/internal/feed"
	"github.com/fcedeirajoaquin/ferced-display/proxy/internal/norm"
)

const (
	endpoint = "https://api.sorsa.io/v3/trends"
	maxBody  = 128 << 10
	minAlnum = 2
)

// WOEID utiles. La lista completa esta en el gist que linkea el swagger.
const (
	WoeidMundo       = 1
	WoeidArgentina   = 23424747
	WoeidBuenosAires = 468739
)

type rawResp struct {
	Trends []struct {
		Name  string `json:"name"`
		Query string `json:"query"`
		URL   string `json:"url"`
	} `json:"trends"`
}

// hasReadableContent evita el caso de las tendencias en alfabetos no latinos:
// al transliterar, "#وش_يحتاج" queda como "# _ _", que en pantalla es basura.
// Un nombre sin al menos dos alfanumericos ASCII no se muestra.
func hasReadableContent(s string) bool {
	n := 0
	for i := 0; i < len(s); i++ {
		c := s[i]
		if (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') {
			n++
			if n >= minAlnum {
				return true
			}
		}
	}
	return false
}

func parse(raw []byte, region string, at time.Time) ([]feed.Item, error) {
	var rr rawResp
	if err := json.Unmarshal(raw, &rr); err != nil {
		return nil, err
	}
	out := make([]feed.Item, 0, len(rr.Trends))
	seen := make(map[string]struct{}, len(rr.Trends))
	for _, tr := range rr.Trends {
		name := norm.Clean(tr.Name)
		if !hasReadableContent(name) {
			continue
		}
		if _, dup := seen[name]; dup {
			continue
		}
		seen[name] = struct{}{}
		out = append(out, feed.Item{
			// Sin la region a proposito: si un tema es tendencia en Argentina
			// y en el mundo, el dedup del mezclador lo deja una sola vez y
			// gana la etiqueta de la fuente configurada primero (la local).
			ID: "trend:" + name,
			Text:   name,
			Author: region,
			Handle: "tendencias",
			At:     at,
			Epoch:  at.Unix(),
			From:   feed.OriginTrend,
			Src:    "trends:" + region,
		})
	}
	return out, nil
}

type Source struct {
	APIKey string
	Woeid  int
	Region string
	Limit  int
	HTTP   *http.Client
	Now    func() time.Time
}

func (s *Source) Name() string { return fmt.Sprintf("trends:%s", s.Region) }

func (s *Source) Fetch(n int) ([]feed.Item, error) {
	c := s.HTTP
	if c == nil {
		c = &http.Client{Timeout: 12 * time.Second}
	}
	now := s.Now
	if now == nil {
		now = time.Now
	}

	req, err := http.NewRequest(http.MethodGet,
		fmt.Sprintf("%s?woeid=%d", endpoint, s.Woeid), nil)
	if err != nil {
		return nil, err
	}
	req.Header.Set("ApiKey", s.APIKey)
	req.Header.Set("Accept", "application/json")

	resp, err := c.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("trends %s: status %d", s.Region, resp.StatusCode)
	}
	raw, err := io.ReadAll(io.LimitReader(resp.Body, maxBody))
	if err != nil {
		return nil, err
	}

	items, err := parse(raw, s.Region, now())
	if err != nil {
		return nil, err
	}

	// Las tendencias comparten timestamp, asi que sin tope coparian el frente
	// del pool y taparian tweets y titulares.
	limit := s.Limit
	if limit <= 0 || limit > len(items) {
		limit = len(items)
	}
	if n > 0 && limit > n {
		limit = n
	}
	return items[:limit], nil
}
