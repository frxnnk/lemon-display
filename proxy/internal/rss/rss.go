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
	maxBody  = 256 << 10
	maxTitle = 180
)

type rawFeed struct {
	Items []struct {
		Title     string `xml:"title"`
		Link      string `xml:"link"`
		PubDate   string `xml:"pubDate"`
		Thumbnail struct {
			URL string `xml:"url,attr"`
		} `xml:"thumbnail"`
	} `xml:"channel>item"`
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
		title := norm.Truncate(norm.Clean(it.Title), maxTitle)
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
