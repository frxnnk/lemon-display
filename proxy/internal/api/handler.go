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
	mix   *mixer.Mixer
	imgs  *img.Cache
	guard *Guard
}

func New(m *mixer.Mixer) *Handler { return &Handler{mix: m} }

func NewWithImages(m *mixer.Mixer, c *img.Cache) *Handler {
	return &Handler{mix: m, imgs: c}
}

func (h *Handler) SetGuard(g *Guard) { h.guard = g }

// El firmware manda de paso como le fue a la ultima animacion. Sin esto el
// framerate real seria una suposicion, y ya nos equivocamos una vez.
func logAnim(r *http.Request) {
	fr := r.URL.Query().Get("fr")
	if fr == "" || fr == "0" {
		return
	}
	frames, _ := strconv.Atoi(fr)
	avg, _ := strconv.Atoi(r.URL.Query().Get("avg"))
	worst, _ := strconv.Atoi(r.URL.Query().Get("max"))
	if frames <= 0 || avg <= 0 {
		return
	}
	log.Printf("[anim] %d frames, medio %.1f ms (%.0f fps), peor %.1f ms",
		frames, float64(avg)/10.0, 10000.0/float64(avg), float64(worst)/10.0)
}

func (h *Handler) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	if h.guard != nil {
		if code := h.guard.Check(r); code != 0 {
			log.Printf("[deny] %s %s %s -> %d", clientIP(r), r.Method, r.URL.Path, code)
			http.Error(w, http.StatusText(code), code)
			return
		}
	}

	// Endurecimiento basico: nada de esto se embebe en otro sitio ni se
	// interpreta como HTML.
	w.Header().Set("X-Content-Type-Options", "nosniff")
	w.Header().Set("X-Frame-Options", "DENY")
	w.Header().Set("Referrer-Policy", "no-referrer")

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
	log.Printf("[img] %s pidio k=%s -> %v", clientIP(r), key, ok)
	logAnim(r)
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
		clientIP(r), n, len(items), r.UserAgent())

	logAnim(r)

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
