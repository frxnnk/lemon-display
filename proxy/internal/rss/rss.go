package rss

import (
	"encoding/xml"
	"fmt"
	"io"
	"net/http"
	"time"

	"github.com/fcedeirajoaquin/ferced-display/proxy/internal/feed"
	"github.com/fcedeirajoaquin/ferced-display/proxy/internal/norm"
)

const (
	// Truncar un feed a la mitad de un bloque CDATA produce XML invalido y se
	// pierde la fuente entera. 2 MB cubre feeds grandes como Xataka.
	maxBody  = 2 << 20
	maxTitle = 180
)

type rawFeed struct {
	Items []struct {
		Title       string `xml:"title"`
		Description string `xml:"description"`
		Link        string `xml:"link"`
		PubDate     string `xml:"pubDate"`
		Thumbnail   struct {
			URL string `xml:"url,attr"`
		} `xml:"thumbnail"`
	} `xml:"channel>item"`
}

// Debajo de esto un titulo no llena una tarjeta de 480x480. Feeds como el de
// BBC Tech usan el nombre del programa de titulo ("Tech Now") y ponen la
// historia en la descripcion.
const minTitleChars = 34

// bodyText elige que mostrar: el titulo si dice algo por si solo, y si no la
// descripcion, que es donde esos feeds guardan el contenido real.
func bodyText(title, desc string) string {
	t := norm.Clean(norm.StripHTML(title))
	d := norm.Clean(norm.StripHTML(desc))

	if len(t) >= minTitleChars || d == "" {
		return norm.Truncate(t, maxTitle)
	}
	if t == "" {
		return norm.Truncate(d, maxTitle)
	}
	// El titulo corto igual aporta contexto, asi que encabeza.
	return norm.Truncate(t+". "+d, maxTitle)
}

var dateLayouts = []string{
	time.RFC1123Z, time.RFC1123, time.RFC822Z, time.RFC822, time.RFC3339,
}

func parseDate(s string) time.Time {
	for _, l := range dateLayouts {
		if t, err := time.Parse(l, s); err == nil {
			return t
		}
	}
	return time.Time{}
}

func parse(raw []byte, label string) ([]feed.Item, error) {
	var rf rawFeed
	if err := xml.Unmarshal(raw, &rf); err != nil {
		return nil, err
	}
	out := make([]feed.Item, 0, len(rf.Items))
	for _, it := range rf.Items {
		title := bodyText(it.Title, it.Description)
		if title == "" {
			continue
		}
		at := parseDate(it.PubDate)
		out = append(out, feed.Item{
			ID:     it.Link,
			Text:   title,
			Author: label,
			Handle: label,
			At:     at,
			Epoch:  at.Unix(),
			From:   feed.OriginRSS,
			ImgURL: it.Thumbnail.URL,
			Src:    "rss:" + label,
		})
	}
	feed.Sort(out)
	return out, nil
}

type Source struct {
	URL   string
	Label string
	HTTP  *http.Client
}

func (s *Source) Name() string { return "rss:" + s.Label }

func (s *Source) Fetch(n int) ([]feed.Item, error) {
	c := s.HTTP
	if c == nil {
		c = &http.Client{Timeout: 12 * time.Second}
	}
	resp, err := c.Get(s.URL)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("rss %s: status %d", s.Label, resp.StatusCode)
	}
	raw, err := io.ReadAll(io.LimitReader(resp.Body, maxBody))
	if err != nil {
		return nil, err
	}
	items, err := parse(raw, s.Label)
	if err != nil {
		return nil, err
	}
	if len(items) > n {
		items = items[:n]
	}
	return items, nil
}
