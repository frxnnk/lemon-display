package api

import (
	"encoding/json"
	"log"
	"net/http"
	"strconv"

	"github.com/fcedeirajoaquin/ferced-display/proxy/internal/feed"
	"github.com/fcedeirajoaquin/ferced-display/proxy/internal/img"
	"github.com/fcedeirajoaquin/ferced-display/proxy/internal/mixer"
	"github.com/fcedeirajoaquin/ferced-display/proxy/internal/padel"
)

const (
	defaultN = 30
	maxN     = 50

	// Partidos y torneos que se sirven por defecto. El aparato rota de a uno,
	// asi que mas de esto es peso muerto en la red y en la RAM del ESP32.
	defaultPartidos = 12
	defaultProximos = 6
)

type Handler struct {
	mix   *mixer.Mixer
	imgs  *img.Cache
	guard *Guard
	fw    *Firmware
	pad   *padel.Client
}

func New(m *mixer.Mixer) *Handler { return &Handler{mix: m} }

func NewWithImages(m *mixer.Mixer, c *img.Cache) *Handler {
	return &Handler{mix: m, imgs: c}
}

func (h *Handler) SetGuard(g *Guard) { h.guard = g }

// SetFirmware enciende el OTA. Sin llamarla, /v1/firmware da 404 y el aparato
// se sigue actualizando solo por USB.
func (h *Handler) SetFirmware(f *Firmware) { h.fw = f }

// SetPadel enciende la app de padel. Sin llamarla, /v1/padel da 404 y el
// aparato muestra la app vacia en vez de colgarse.
func (h *Handler) SetPadel(p *padel.Client) { h.pad = p }

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
	case "/v1/padel":
		h.servePadel(w, r)
	case "/v1/img":
		h.serveImg(w, r)
	// El OTA queda del mismo lado del guard que el feed: un binario ejecutable
	// no puede estar mas expuesto que un titular. h.fw en nil no rompe, los dos
	// metodos chequean que la funcion este encendida.
	case "/v1/firmware":
		h.fw.serveMeta(w, r)
	case "/v1/firmware/bin":
		h.fw.serveBin(w, r)
	default:
		http.NotFound(w, r)
	}
}

// servePadel entrega el estado del circuito profesional: el torneo que se esta
// jugando con su orden de juego, y los proximos del calendario.
//
// El firmware recibe texto listo para pintar —horas en 24 h, fases en
// castellano, fechas ya formateadas— porque toda la fragilidad de raspar dos
// sitios ajenos tiene que quedar de este lado.
func (h *Handler) servePadel(w http.ResponseWriter, r *http.Request) {
	if h.pad == nil {
		http.NotFound(w, r)
		return
	}

	snap, err := h.pad.Feed(defaultProximos)
	if err != nil {
		log.Printf("[padel] %s pidio y fallo: %v", clientIP(r), err)
		http.Error(w, "sin datos de padel", http.StatusServiceUnavailable)
		return
	}

	// Se copia antes de recortar: snap es el cacheado y lo comparten todos los
	// pedidos. Mutarlo aca iria vaciando el cache pedido a pedido.
	out := *snap
	if len(out.Matches) > defaultPartidos {
		out.Matches = out.Matches[:defaultPartidos]
	}

	nombre := "-"
	if out.Live != nil {
		nombre = out.Live.Name
	}
	log.Printf("[padel] %s pidio, sirvo %s (%d partidos, %d proximos) (%s)",
		clientIP(r), nombre, len(out.Matches), len(out.Next), r.UserAgent())
	logAnim(r)

	// Content-Length declarado, igual que el feed: el HTTPClient del ESP32
	// entrega el framing de chunks adentro del cuerpo y ArduinoJson se atraganta.
	body, err := json.Marshal(out)
	if err != nil {
		http.Error(w, "no se pudo serializar el padel", http.StatusInternalServerError)
		return
	}
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Content-Length", strconv.Itoa(len(body)))
	w.Header().Set("Cache-Control", "public, max-age=120")
	w.Write(body)
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

	// fresh=1 saltea el TTL del pool. Lo manda el boton "Actualizar feed" de la
	// pantalla de configuracion: sin esto, apretarlo dentro de los 10 minutos
	// del TTL devolvia los mismos items y parecia que el boton no hacia nada.
	forzado := r.URL.Query().Get("fresh") == "1"
	var items []feed.Item
	if forzado {
		items = h.mix.FeedFresh(n)
	} else {
		items = h.mix.Feed(n)
	}
	for i := range items {
		items[i].Epoch = items[i].At.Unix()
		if h.imgs != nil {
			// Best-effort: si la imagen no se puede bajar, el item igual sale.
			items[i].ImgKey = h.imgs.Resolve(items[i].ImgURL)
		}
	}

	log.Printf("[feed] %s pidio n=%d%s, sirvo %d items (%s)",
		clientIP(r), n, map[bool]string{true: " fresh"}[forzado], len(items), r.UserAgent())

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
