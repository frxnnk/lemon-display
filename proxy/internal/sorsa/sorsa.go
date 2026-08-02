package sorsa

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
	endpoint = "https://api.sorsa.io/v3/list-tweets"
	maxBody  = 512 << 10
	maxText  = 180
)

// OJO: este shape esta asumido, no verificado contra la API real. El fixture
// de testdata/list_tweets.json es el que manda; si no coincide, ajustar aca.
type rawResp struct {
	Tweets []struct {
		ID        string `json:"id"`
		Text      string `json:"text"`
		CreatedAt string `json:"created_at"`
		Author    struct {
			Name     string `json:"name"`
			Username string `json:"username"`
		} `json:"author"`
	} `json:"tweets"`
}

var dateLayouts = []string{
	time.RFC3339,
	"Mon Jan 02 15:04:05 -0700 2006",
	time.RFC1123Z,
}

func parseDate(s string) time.Time {
	for _, l := range dateLayouts {
		if t, err := time.Parse(l, s); err == nil {
			return t
		}
	}
	return time.Time{}
}

func parse(raw []byte) ([]feed.Item, error) {
	var rr rawResp
	if err := json.Unmarshal(raw, &rr); err != nil {
		return nil, err
	}
	out := make([]feed.Item, 0, len(rr.Tweets))
	for _, tw := range rr.Tweets {
		text := norm.Truncate(norm.Clean(tw.Text), maxText)
		if text == "" {
			continue
		}
		at := parseDate(tw.CreatedAt)
		out = append(out, feed.Item{
			ID:     "x:" + tw.ID,
			Text:   text,
			Author: norm.Clean(tw.Author.Name),
			Handle: "@" + tw.Author.Username,
			At:     at,
			Epoch:  at.Unix(),
			From:   feed.OriginX,
		})
	}
	feed.Sort(out)
	return out, nil
}

type Source struct {
	APIKey string
	ListID string
	HTTP   *http.Client
}

func (s *Source) Name() string { return "sorsa:list" }

func (s *Source) Fetch(n int) ([]feed.Item, error) {
	c := s.HTTP
	if c == nil {
		c = &http.Client{Timeout: 12 * time.Second}
	}
	url := fmt.Sprintf("%s?list_id=%s&count=%d", endpoint, s.ListID, n)
	req, err := http.NewRequest(http.MethodGet, url, nil)
	if err != nil {
		return nil, err
	}
	req.Header.Set("ApiKey", s.APIKey)

	resp, err := c.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("sorsa: status %d", resp.StatusCode)
	}
	raw, err := io.ReadAll(io.LimitReader(resp.Body, maxBody))
	if err != nil {
		return nil, err
	}
	return parse(raw)
}
