package sorsa

import (
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"time"

	"github.com/fcedeirajoaquin/ferced-display/proxy/internal/feed"
	"github.com/fcedeirajoaquin/ferced-display/proxy/internal/norm"
)

const (
	endpoint = "https://api.sorsa.io/v3/list-tweets"
	maxBody  = 512 << 10
	maxText  = 180
)

// Shape verificado contra https://api.sorsa.io/v3/swagger.json
// (common.TweetsResponse -> common.Tweet -> common.User).
type rawResp struct {
	NextCursor string `json:"next_cursor"`
	Tweets     []struct {
		ID        string `json:"id"`
		FullText  string `json:"full_text"`
		CreatedAt string `json:"created_at"`
		Lang      string `json:"lang"`
		IsReply   bool   `json:"is_reply"`
		User      struct {
			Username        string `json:"username"`
			DisplayName     string `json:"display_name"`
			ProfileImageURL string `json:"profile_image_url"`
		} `json:"user"`
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

// parse descarta respuestas sueltas: fuera del hilo se leen sin contexto y en
// una pantalla de escritorio quedan como frases al aire.
func parse(raw []byte) ([]feed.Item, error) {
	var rr rawResp
	if err := json.Unmarshal(raw, &rr); err != nil {
		return nil, err
	}
	out := make([]feed.Item, 0, len(rr.Tweets))
	for _, tw := range rr.Tweets {
		if tw.IsReply {
			continue
		}
		text := norm.Truncate(norm.Clean(tw.FullText), maxText)
		if text == "" {
			continue
		}
		at := parseDate(tw.CreatedAt)
		out = append(out, feed.Item{
			ID:     "x:" + tw.ID,
			Text:   text,
			Author: norm.Clean(tw.User.DisplayName),
			Handle: "@" + tw.User.Username,
			At:     at,
			Epoch:  at.Unix(),
			From:   feed.OriginX,
			ImgURL: tw.User.ProfileImageURL,
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

// Fetch ignora n: /list-tweets no acepta tamano de pagina, solo list_id y
// next_cursor. Se recorta del lado del mezclador.
func (s *Source) Fetch(n int) ([]feed.Item, error) {
	c := s.HTTP
	if c == nil {
		c = &http.Client{Timeout: 12 * time.Second}
	}

	req, err := http.NewRequest(http.MethodGet,
		fmt.Sprintf("%s?list_id=%s", endpoint, url.QueryEscape(s.ListID)), nil)
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
		return nil, fmt.Errorf("sorsa: status %d", resp.StatusCode)
	}
	raw, err := io.ReadAll(io.LimitReader(resp.Body, maxBody))
	if err != nil {
		return nil, err
	}

	items, err := parse(raw)
	if err != nil {
		return nil, err
	}
	if n > 0 && len(items) > n {
		items = items[:n]
	}
	return items, nil
}
