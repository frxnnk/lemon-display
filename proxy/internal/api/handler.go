package api

import (
	"encoding/json"
	"log"
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

	log.Printf("[feed] %s pidio n=%d, sirvo %d items (%s)",
		r.RemoteAddr, n, len(items), r.UserAgent())

	// Se serializa a buffer para poder declarar Content-Length. Con un encoder
	// en streaming, Go pasa a Transfer-Encoding: chunked al superar ~4KB, y el
	// HTTPClient del ESP32 entrega ese stream con el framing de chunks adentro,
	// que ArduinoJson no sabe parsear.
	body, err := json.Marshal(struct {
		Items []feed.Item `json:"items"`
	}{items})
	if err != nil {
		http.Error(w, "no se pudo serializar el feed", http.StatusInternalServerError)
		return
	}

	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Content-Length", strconv.Itoa(len(body)))
	w.Header().Set("Cache-Control", "public, max-age=60")
	w.Write(body)
}
