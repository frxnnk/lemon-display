package api

import (
	"encoding/json"
	"log"
	"net/http"
	"strconv"

	"github.com/fcedeirajoaquin/ferced-display/proxy/internal/feed"
	"github.com/fcedeirajoaquin/ferced-display/proxy/internal/img"
	"github.com/fcedeirajoaquin/ferced-display/proxy/internal/mixer"
)

const (
	defaultN = 30
	maxN     = 50
)

type Handler struct {
	mix  *mixer.Mixer
	imgs *img.Cache
}

func New(m *mixer.Mixer) *Handler { return &Handler{mix: m} }

func NewWithImages(m *mixer.Mixer, c *img.Cache) *Handler {
	return &Handler{mix: m, imgs: c}
}

func (h *Handler) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	switch r.URL.Path {
	case "/healthz":
		w.WriteHeader(http.StatusOK)
		w.Write([]byte("ok"))
	case "/v1/feed":
		h.serveFeed(w, r)
	case "/v1/img":
		h.serveImg(w, r)
	default:
		http.NotFound(w, r)
	}
}

// serveImg entrega pixeles RGB565 crudos, listos para pintar. El firmware no
// decodifica nada. Solo sirve keys ya resueltas: el aparato nunca pide una URL.
func (h *Handler) serveImg(w http.ResponseWriter, r *http.Request) {
	if h.imgs == nil {
		http.NotFound(w, r)
		return
	}
	key := r.URL.Query().Get("k")
	raw, ok := h.imgs.Get(key)
	log.Printf("[img] %s pidio k=%s -> %v", r.RemoteAddr, key, ok)
	if !ok {
		http.NotFound(w, r)
		return
	}
	w.Header().Set("Content-Type", "application/octet-stream")
	w.Header().Set("Content-Length", strconv.Itoa(len(raw)))
	w.Header().Set("Cache-Control", "public, max-age=86400")
	w.Write(raw)
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
		if h.imgs != nil {
			// Best-effort: si la imagen no se puede bajar, el item igual sale.
			items[i].ImgKey = h.imgs.Resolve(items[i].ImgURL)
		}
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
