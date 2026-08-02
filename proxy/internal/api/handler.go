package api

import (
	"encoding/json"
	"net/http"
	"strconv"

	"github.com/fcedeirajoaquin/ferced-display/proxy/internal/feed"
	"github.com/fcedeirajoaquin/ferced-display/proxy/internal/mixer"
)

const (
	defaultN = 30
	maxN     = 50
)

type Handler struct{ mix *mixer.Mixer }

func New(m *mixer.Mixer) *Handler { return &Handler{mix: m} }

func (h *Handler) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	switch r.URL.Path {
	case "/healthz":
		w.WriteHeader(http.StatusOK)
		w.Write([]byte("ok"))
	case "/v1/feed":
		h.serveFeed(w, r)
	default:
		http.NotFound(w, r)
	}
}

func (h *Handler) serveFeed(w http.ResponseWriter, r *http.Request) {
	n, err := strconv.Atoi(r.URL.Query().Get("n"))
	if err != nil || n <= 0 {
		n = defaultN
	}
	if n > maxN {
		n = maxN
	}

	items := h.mix.Feed(n)
	for i := range items {
		items[i].Epoch = items[i].At.Unix()
	}

	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Cache-Control", "public, max-age=60")
	json.NewEncoder(w).Encode(struct {
		Items []feed.Item `json:"items"`
	}{items})
}
