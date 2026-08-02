package api

import (
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"testing"
	"time"

	"github.com/fcedeirajoaquin/ferced-display/proxy/internal/feed"
	"github.com/fcedeirajoaquin/ferced-display/proxy/internal/mixer"
)

type fake struct{ n int }

func (fake) Name() string { return "fake" }

func (f fake) Fetch(n int) ([]feed.Item, error) {
	count := f.n
	if count == 0 {
		count = 1
	}
	out := make([]feed.Item, 0, count)
	base := time.Now()
	for i := 0; i < count; i++ {
		out = append(out, feed.Item{
			ID:     string(rune('a' + i)),
			Text:   "hola",
			Author: "Alguien",
			Handle: "@alguien",
			At:     base.Add(-time.Duration(i) * time.Minute),
			From:   feed.OriginX,
		})
	}
	return out, nil
}

func get(t *testing.T, path string, src feed.Source) *httptest.ResponseRecorder {
	t.Helper()
	h := New(mixer.New(time.Minute, src))
	rec := httptest.NewRecorder()
	h.ServeHTTP(rec, httptest.NewRequest(http.MethodGet, path, nil))
	return rec
}

func TestFeedEndpointReturnsCompactJSON(t *testing.T) {
	rec := get(t, "/v1/feed?n=5", fake{})

	if rec.Code != http.StatusOK {
		t.Fatalf("status = %d", rec.Code)
	}
	if ct := rec.Header().Get("Content-Type"); ct != "application/json" {
		t.Errorf("content-type = %q", ct)
	}

	var body struct {
		Items []feed.Item `json:"items"`
	}
	if err := json.Unmarshal(rec.Body.Bytes(), &body); err != nil {
		t.Fatal(err)
	}
	if len(body.Items) != 1 || body.Items[0].Text != "hola" {
		t.Fatalf("body inesperado: %s", rec.Body.String())
	}
}

func TestFeedFillsEpoch(t *testing.T) {
	rec := get(t, "/v1/feed?n=1", fake{})

	var body struct {
		Items []struct {
			TS int64 `json:"ts"`
		} `json:"items"`
	}
	if err := json.Unmarshal(rec.Body.Bytes(), &body); err != nil {
		t.Fatal(err)
	}
	if body.Items[0].TS <= 0 {
		t.Fatalf("ts = %d, quiero epoch real", body.Items[0].TS)
	}
}

func TestFeedUsesShortJSONKeys(t *testing.T) {
	rec := get(t, "/v1/feed?n=1", fake{})

	var body struct {
		Items []map[string]any `json:"items"`
	}
	if err := json.Unmarshal(rec.Body.Bytes(), &body); err != nil {
		t.Fatal(err)
	}
	for _, k := range []string{"id", "t", "a", "h", "ts", "o"} {
		if _, ok := body.Items[0][k]; !ok {
			t.Errorf("falta la clave %q en %v", k, body.Items[0])
		}
	}
}

func TestFeedClampsN(t *testing.T) {
	rec := get(t, "/v1/feed?n=9999", fake{n: 60})
	if rec.Code != http.StatusOK {
		t.Fatalf("status = %d", rec.Code)
	}

	var body struct {
		Items []feed.Item `json:"items"`
	}
	if err := json.Unmarshal(rec.Body.Bytes(), &body); err != nil {
		t.Fatal(err)
	}
	if len(body.Items) > 50 {
		t.Fatalf("items = %d, quiero <= 50", len(body.Items))
	}
}

func TestFeedDefaultsNWhenMissingOrJunk(t *testing.T) {
	for _, path := range []string{"/v1/feed", "/v1/feed?n=abc", "/v1/feed?n=-3"} {
		rec := get(t, path, fake{})
		if rec.Code != http.StatusOK {
			t.Errorf("%s: status = %d", path, rec.Code)
		}
	}
}

func TestHealthz(t *testing.T) {
	rec := get(t, "/healthz", fake{})
	if rec.Code != http.StatusOK {
		t.Fatalf("status = %d", rec.Code)
	}
	if rec.Body.String() != "ok" {
		t.Errorf("body = %q", rec.Body.String())
	}
}

func TestUnknownPathIs404(t *testing.T) {
	rec := get(t, "/otra/cosa", fake{})
	if rec.Code != http.StatusNotFound {
		t.Fatalf("status = %d, quiero 404", rec.Code)
	}
}
