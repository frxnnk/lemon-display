package api

import (
	"net/http"
	"net/http/httptest"
	"testing"
	"time"
)

func guarded(token string, rps float64, burst int) *Guard {
	return NewGuard(token, rps, burst)
}

func req(method, path, auth string) *http.Request {
	r := httptest.NewRequest(method, path, nil)
	r.RemoteAddr = "203.0.113.7:5000"
	if auth != "" {
		r.Header.Set("Authorization", auth)
	}
	return r
}

func TestGuardRejectsMissingToken(t *testing.T) {
	g := guarded("secreto", 100, 100)
	if code := g.Check(req(http.MethodGet, "/v1/feed", "")); code != http.StatusUnauthorized {
		t.Fatalf("code = %d, quiero 401", code)
	}
}

func TestGuardRejectsWrongToken(t *testing.T) {
	g := guarded("secreto", 100, 100)
	if code := g.Check(req(http.MethodGet, "/v1/feed", "Bearer otro")); code != http.StatusUnauthorized {
		t.Fatalf("code = %d, quiero 401", code)
	}
}

func TestGuardAcceptsRightToken(t *testing.T) {
	g := guarded("secreto", 100, 100)
	if code := g.Check(req(http.MethodGet, "/v1/feed", "Bearer secreto")); code != 0 {
		t.Fatalf("code = %d, quiero 0 (pasa)", code)
	}
}

// Sin token configurado el proxy queda abierto: util en desarrollo por LAN,
// pero tiene que ser una decision explicita, no el default silencioso.
func TestGuardWithoutTokenAllowsAll(t *testing.T) {
	g := guarded("", 100, 100)
	if code := g.Check(req(http.MethodGet, "/v1/feed", "")); code != 0 {
		t.Fatalf("code = %d, quiero 0", code)
	}
}

func TestGuardRejectsNonGet(t *testing.T) {
	g := guarded("", 100, 100)
	for _, m := range []string{http.MethodPost, http.MethodPut, http.MethodDelete} {
		if code := g.Check(req(m, "/v1/feed", "")); code != http.StatusMethodNotAllowed {
			t.Errorf("%s: code = %d, quiero 405", m, code)
		}
	}
}

func TestGuardRateLimitsPerIP(t *testing.T) {
	g := guarded("", 0.001, 3) // 3 de golpe, reposicion practicamente nula
	for i := 0; i < 3; i++ {
		if code := g.Check(req(http.MethodGet, "/v1/feed", "")); code != 0 {
			t.Fatalf("pedido %d rechazado antes de tiempo (%d)", i+1, code)
		}
	}
	if code := g.Check(req(http.MethodGet, "/v1/feed", "")); code != http.StatusTooManyRequests {
		t.Fatalf("code = %d, quiero 429 tras agotar el balde", code)
	}
}

func TestGuardRateLimitIsPerIPNotGlobal(t *testing.T) {
	g := guarded("", 0.001, 2)
	a := req(http.MethodGet, "/v1/feed", "")
	a.RemoteAddr = "203.0.113.7:5000"
	b := req(http.MethodGet, "/v1/feed", "")
	b.RemoteAddr = "203.0.113.8:5000"

	g.Check(a)
	g.Check(a)
	if code := g.Check(a); code != http.StatusTooManyRequests {
		t.Fatalf("la primera IP deberia estar limitada, code = %d", code)
	}
	if code := g.Check(b); code != 0 {
		t.Fatalf("la segunda IP no deberia verse afectada, code = %d", code)
	}
}

func TestGuardRefillsOverTime(t *testing.T) {
	g := guarded("", 1000, 1) // 1000/s: se repone casi al instante
	g.Check(req(http.MethodGet, "/v1/feed", ""))
	time.Sleep(5 * time.Millisecond)
	if code := g.Check(req(http.MethodGet, "/v1/feed", "")); code != 0 {
		t.Fatalf("deberia haberse repuesto, code = %d", code)
	}
}

func TestHealthzNeedsNoToken(t *testing.T) {
	g := guarded("secreto", 100, 100)
	if code := g.Check(req(http.MethodGet, "/healthz", "")); code != 0 {
		t.Fatalf("healthz no deberia pedir token, code = %d", code)
	}
}
