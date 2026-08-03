package api

import (
	"crypto/subtle"
	"net"
	"net/http"
	"strings"
	"sync"
	"time"
)

// Guard concentra los controles de acceso: metodo, token y limite de tasa.
// Devuelve 0 cuando el pedido puede seguir, o el codigo HTTP del rechazo.
type Guard struct {
	token string
	rps   float64
	burst int

	mu      sync.Mutex
	buckets map[string]*bucket
}

type bucket struct {
	tokens float64
	last   time.Time
}

func NewGuard(token string, rps float64, burst int) *Guard {
	return &Guard{
		token:   token,
		rps:     rps,
		burst:   burst,
		buckets: make(map[string]*bucket),
	}
}

func clientIP(r *http.Request) string {
	// Caddy es el unico que llega a este proceso (escucha en 127.0.0.1), asi
	// que X-Forwarded-For viene de el y es confiable.
	if xff := r.Header.Get("X-Forwarded-For"); xff != "" {
		if i := strings.IndexByte(xff, ','); i > 0 {
			return strings.TrimSpace(xff[:i])
		}
		return strings.TrimSpace(xff)
	}
	host, _, err := net.SplitHostPort(r.RemoteAddr)
	if err != nil {
		return r.RemoteAddr
	}
	return host
}

func (g *Guard) Check(r *http.Request) int {
	if r.Method != http.MethodGet && r.Method != http.MethodHead {
		return http.StatusMethodNotAllowed
	}

	// healthz queda abierto para que el monitoreo no necesite el secreto.
	if r.URL.Path != "/healthz" && g.token != "" {
		const prefix = "Bearer "
		auth := r.Header.Get("Authorization")
		if !strings.HasPrefix(auth, prefix) {
			return http.StatusUnauthorized
		}
		got := auth[len(prefix):]
		// Comparacion de tiempo constante: evita filtrar el token por timing.
		if subtle.ConstantTimeCompare([]byte(got), []byte(g.token)) != 1 {
			return http.StatusUnauthorized
		}
	}

	if !g.allow(clientIP(r)) {
		return http.StatusTooManyRequests
	}
	return 0
}

func (g *Guard) allow(ip string) bool {
	if g.rps <= 0 || g.burst <= 0 {
		return true
	}

	now := time.Now()
	g.mu.Lock()
	defer g.mu.Unlock()

	b, ok := g.buckets[ip]
	if !ok {
		// Poda simple: el mapa no puede crecer sin techo con IPs de paso.
		if len(g.buckets) > 4096 {
			for k, v := range g.buckets {
				if now.Sub(v.last) > 10*time.Minute {
					delete(g.buckets, k)
				}
			}
		}
		g.buckets[ip] = &bucket{tokens: float64(g.burst) - 1, last: now}
		return true
	}

	b.tokens += now.Sub(b.last).Seconds() * g.rps
	if b.tokens > float64(g.burst) {
		b.tokens = float64(g.burst)
	}
	b.last = now

	if b.tokens < 1 {
		return false
	}
	b.tokens--
	return true
}
