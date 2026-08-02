# ferced-display Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Construir `ferced-display` — firmware de Ferced para la Lemon Box que rota tweets de una Lista pública de X y titulares RSS a 3-4 por minuto, alimentado por un proxy en el VPS.

**Architecture:** Un proxy en Go corriendo en el VPS detrás de Caddy mezcla dos fuentes (Sorsa `/v3/list-tweets` y RSS) en un feed cronológico, normaliza el texto a ASCII y lo sirve en `GET /v1/feed`. El firmware ESP32-S3 —fork de `lemon-display` sin las escenas de crypto— sólo consume esa URL, así que la fuente se puede cambiar sin reflashear.

**Tech Stack:** Go 1.25 (proxy) · C++/Arduino + LovyanGFX + PlatformIO 6.1.19 (firmware) · systemd + Caddy (deploy) · esptool 5.1.0 (flasheo)

**Diseño de referencia:** `docs/plans/2026-08-02-ferced-display-design.md`

---

## Prerequisitos (sólo los podés hacer vos)

Estas tres cosas bloquean el plan y requieren tus credenciales. Ninguna la puede
hacer un agente.

**P1. Crear la Lista pública de X.** En X: Perfil → Listas → Nueva lista, marcarla
como **pública**, agregar las 30-50 cuentas que quieras ver. Anotá el ID numérico
de la lista, que aparece en la URL: `x.com/i/lists/<ID>`.

**P2. Obtener la API key de Sorsa.** Registrarte en api.sorsa.io y generar la key.
Guardala; en el paso T7 va a una variable de entorno del VPS, nunca al repo.

**P3. Crear el repo `ferced-display`.** En tu cuenta de GitHub, repo nuevo y vacío.
El fork se arma en T8 desde el clon local que ya tenemos.

**Fixture real (T0).** Con P1 y P2 listos, capturá una respuesta real de Sorsa para
usarla de fixture en los tests. Sin esto los tests de T4 se escriben a ciegas:

```bash
curl -s -H "ApiKey: $SORSA_KEY" "https://api.sorsa.io/v3/list-tweets?list_id=<ID>&count=20" -o proxy/internal/sorsa/testdata/list_tweets.json
```

Si el shape del JSON no coincide con el que asume T4, ajustá el struct — el test
te lo va a decir enseguida.

---

## Fase A — El proxy

### Task 1: Scaffold del módulo y la interfaz Source

**Files:**
- Create: `proxy/go.mod`
- Create: `proxy/internal/feed/item.go`
- Test: `proxy/internal/feed/item_test.go`

**Step 1: Crear el módulo**

```bash
mkdir -p proxy/internal/feed
cd proxy && go mod init github.com/fcedeirajoaquin/ferced-display/proxy
```

**Step 2: Escribir el test que falla**

`proxy/internal/feed/item_test.go`:

```go
package feed

import (
	"testing"
	"time"
)

func TestItemsSortNewestFirst(t *testing.T) {
	base := time.Date(2026, 8, 2, 12, 0, 0, 0, time.UTC)
	items := []Item{
		{ID: "a", Text: "viejo", At: base.Add(-2 * time.Hour)},
		{ID: "b", Text: "nuevo", At: base},
		{ID: "c", Text: "medio", At: base.Add(-1 * time.Hour)},
	}
	Sort(items)
	got := []string{items[0].ID, items[1].ID, items[2].ID}
	want := []string{"b", "c", "a"}
	for i := range want {
		if got[i] != want[i] {
			t.Fatalf("orden = %v, quiero %v", got, want)
		}
	}
}

func TestDedupPrefersFirstSeen(t *testing.T) {
	items := []Item{
		{ID: "x", Text: "primero"},
		{ID: "x", Text: "duplicado"},
		{ID: "y", Text: "otro"},
	}
	out := Dedup(items)
	if len(out) != 2 {
		t.Fatalf("largo = %d, quiero 2", len(out))
	}
	if out[0].Text != "primero" {
		t.Fatalf("texto = %q, quiero %q", out[0].Text, "primero")
	}
}
```

**Step 3: Correr el test para verificar que falla**

Run: `cd proxy && go test ./internal/feed/`
Expected: FAIL — `undefined: Item`, `undefined: Sort`, `undefined: Dedup`

**Step 4: Implementación mínima**

`proxy/internal/feed/item.go`:

```go
package feed

import (
	"sort"
	"time"
)

type Origin string

const (
	OriginX   Origin = "x"
	OriginRSS Origin = "rss"
)

type Item struct {
	ID     string    `json:"id"`
	Text   string    `json:"t"`
	Author string    `json:"a"`
	Handle string    `json:"h"`
	At     time.Time `json:"-"`
	Epoch  int64     `json:"ts"`
	From   Origin    `json:"o"`
}

func Sort(items []Item) {
	sort.SliceStable(items, func(i, j int) bool {
		return items[i].At.After(items[j].At)
	})
}

func Dedup(items []Item) []Item {
	seen := make(map[string]struct{}, len(items))
	out := items[:0:0]
	for _, it := range items {
		if _, dup := seen[it.ID]; dup {
			continue
		}
		seen[it.ID] = struct{}{}
		out = append(out, it)
	}
	return out
}

type Source interface {
	Name() string
	Fetch(n int) ([]Item, error)
}
```

**Step 5: Correr el test para verificar que pasa**

Run: `cd proxy && go test ./internal/feed/`
Expected: PASS (2 tests)

**Step 6: Commit**

```bash
git add proxy/go.mod proxy/internal/feed/
git commit -m "proxy: modelo de item, orden y dedup"
```

---

### Task 2: Normalizador de texto

Es la pieza de mayor valor y la más fácil de testear: función pura, entrada
conocida, salida conocida. Acá se resuelve el problema de la fuente ASCII-only
del firmware.

**Files:**
- Create: `proxy/internal/norm/norm.go`
- Test: `proxy/internal/norm/norm_test.go`

**Step 1: Escribir el test que falla**

`proxy/internal/norm/norm_test.go`:

```go
package norm

import "testing"

func TestClean(t *testing.T) {
	cases := []struct{ name, in, want string }{
		{"tildes", "El niño comió mañana", "El nino comio manana"},
		{"mayus con tilde", "ÁRBOL Ñandú", "ARBOL Nandu"},
		{"emoji fuera", "Buenísimo 🚀🔥 esto", "Buenisimo esto"},
		{"url colapsada", "mira esto https://t.co/abc123 ahora", "mira esto [link] ahora"},
		{"comillas curvas", "el “mejor” día", "el \"mejor\" dia"},
		{"puntos suspensivos", "espera… ya", "espera... ya"},
		{"espacios colapsados", "hola    che\n\nque tal", "hola che que tal"},
		{"nbsp", "uno dos", "uno dos"},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			if got := Clean(c.in); got != c.want {
				t.Errorf("Clean(%q) = %q, quiero %q", c.in, got, c.want)
			}
		})
	}
}

func TestTruncateOnWordBoundary(t *testing.T) {
	in := "palabra " + string(make([]byte, 0)) + "otra vez con mucho texto que se pasa del limite fijado"
	got := Truncate(in, 20)
	if len(got) > 21 {
		t.Fatalf("largo = %d, quiero <= 21", len(got))
	}
	if got[len(got)-1] != '.' {
		t.Fatalf("deberia terminar en elipsis, got %q", got)
	}
}

func TestCleanIsASCIIOnly(t *testing.T) {
	out := Clean("Ñoño 🎉 café — “test”")
	for i := 0; i < len(out); i++ {
		if out[i] > 127 {
			t.Fatalf("byte no-ASCII %d en %q", out[i], out)
		}
	}
}
```

**Step 2: Correr para verificar que falla**

Run: `cd proxy && go test ./internal/norm/`
Expected: FAIL — `undefined: Clean`, `undefined: Truncate`

**Step 3: Implementación**

`proxy/internal/norm/norm.go`:

```go
package norm

import (
	"regexp"
	"strings"
)

var (
	urlRe   = regexp.MustCompile(`https?://\S+`)
	spaceRe = regexp.MustCompile(`\s+`)
)

var translit = map[rune]string{
	'á': "a", 'é': "e", 'í': "i", 'ó': "o", 'ú': "u", 'ü': "u", 'ñ': "n",
	'Á': "A", 'É': "E", 'Í': "I", 'Ó': "O", 'Ú': "U", 'Ü': "U", 'Ñ': "N",
	'à': "a", 'è': "e", 'ì': "i", 'ò': "o", 'ù': "u",
	'â': "a", 'ê': "e", 'î': "i", 'ô': "o", 'û': "u",
	'ç': "c", 'Ç': "C",
	'‘': "'", '’': "'",
	'“': "\"", '”': "\"",
	'–': "-", '—': "-",
	'…': "...",
	' ': " ",
}

func Clean(s string) string {
	s = urlRe.ReplaceAllString(s, "[link]")

	var b strings.Builder
	b.Grow(len(s))
	for _, r := range s {
		if rep, ok := translit[r]; ok {
			b.WriteString(rep)
			continue
		}
		if r < 128 {
			b.WriteRune(r)
			continue
		}
		b.WriteByte(' ')
	}

	return strings.TrimSpace(spaceRe.ReplaceAllString(b.String(), " "))
}

func Truncate(s string, max int) string {
	if len(s) <= max {
		return s
	}
	cut := s[:max]
	if i := strings.LastIndexByte(cut, ' '); i > max/2 {
		cut = cut[:i]
	}
	return strings.TrimRight(cut, " ,;:") + "."
}
```

**Step 4: Correr para verificar que pasa**

Run: `cd proxy && go test ./internal/norm/ -v`
Expected: PASS — todos los subtests

**Step 5: Commit**

```bash
git add proxy/internal/norm/
git commit -m "proxy: normalizador de texto a ASCII"
```

---

### Task 3: Fuente RSS

Reusa el conocimiento de `src/news_client.cpp`, pero en Go y con parser XML real.

**Files:**
- Create: `proxy/internal/rss/rss.go`
- Create: `proxy/internal/rss/testdata/sample.xml`
- Test: `proxy/internal/rss/rss_test.go`

**Step 1: Fixture**

`proxy/internal/rss/testdata/sample.xml`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<rss version="2.0"><channel>
  <title>Ejemplo</title>
  <item>
    <title>Primer título con acentuación</title>
    <link>https://ejemplo.com/1</link>
    <pubDate>Sat, 02 Aug 2026 10:00:00 GMT</pubDate>
  </item>
  <item>
    <title>Segundo &amp; con entidad</title>
    <link>https://ejemplo.com/2</link>
    <pubDate>Sat, 02 Aug 2026 09:00:00 GMT</pubDate>
  </item>
</channel></rss>
```

**Step 2: Test que falla**

`proxy/internal/rss/rss_test.go`:

```go
package rss

import (
	"os"
	"testing"
)

func TestParseItems(t *testing.T) {
	raw, err := os.ReadFile("testdata/sample.xml")
	if err != nil {
		t.Fatal(err)
	}
	items, err := parse(raw, "ejemplo")
	if err != nil {
		t.Fatal(err)
	}
	if len(items) != 2 {
		t.Fatalf("items = %d, quiero 2", len(items))
	}
	if items[0].Text != "Primer titulo con acentuacion" {
		t.Errorf("texto = %q", items[0].Text)
	}
	if items[1].Text != "Segundo & con entidad" {
		t.Errorf("entidad mal decodificada: %q", items[1].Text)
	}
	if items[0].From != "rss" {
		t.Errorf("origen = %q, quiero rss", items[0].From)
	}
	if !items[0].At.After(items[1].At) {
		t.Error("el primero deberia ser mas nuevo")
	}
}
```

**Step 3: Verificar que falla**

Run: `cd proxy && go test ./internal/rss/`
Expected: FAIL — `undefined: parse`

**Step 4: Implementación**

`proxy/internal/rss/rss.go`:

```go
package rss

import (
	"encoding/xml"
	"fmt"
	"io"
	"net/http"
	"time"

	"github.com/fcedeirajoaquin/ferced-display/proxy/internal/feed"
	"github.com/fcedeirajoaquin/ferced-display/proxy/internal/norm"
)

const maxBody = 256 << 10

type rawFeed struct {
	Items []struct {
		Title   string `xml:"title"`
		Link    string `xml:"link"`
		PubDate string `xml:"pubDate"`
	} `xml:"channel>item"`
}

var dateLayouts = []string{
	time.RFC1123Z, time.RFC1123, time.RFC822Z, time.RFC822, time.RFC3339,
}

func parseDate(s string) time.Time {
	for _, l := range dateLayouts {
		if t, err := time.Parse(l, s); err == nil {
			return t
		}
	}
	return time.Time{}
}

func parse(raw []byte, label string) ([]feed.Item, error) {
	var rf rawFeed
	if err := xml.Unmarshal(raw, &rf); err != nil {
		return nil, err
	}
	out := make([]feed.Item, 0, len(rf.Items))
	for _, it := range rf.Items {
		title := norm.Truncate(norm.Clean(it.Title), 180)
		if title == "" {
			continue
		}
		at := parseDate(it.PubDate)
		out = append(out, feed.Item{
			ID:     it.Link,
			Text:   title,
			Author: label,
			Handle: label,
			At:     at,
			Epoch:  at.Unix(),
			From:   feed.OriginRSS,
		})
	}
	feed.Sort(out)
	return out, nil
}

type Source struct {
	URL   string
	Label string
	HTTP  *http.Client
}

func (s *Source) Name() string { return "rss:" + s.Label }

func (s *Source) Fetch(n int) ([]feed.Item, error) {
	c := s.HTTP
	if c == nil {
		c = &http.Client{Timeout: 12 * time.Second}
	}
	resp, err := c.Get(s.URL)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("rss %s: status %d", s.Label, resp.StatusCode)
	}
	raw, err := io.ReadAll(io.LimitReader(resp.Body, maxBody))
	if err != nil {
		return nil, err
	}
	items, err := parse(raw, s.Label)
	if err != nil {
		return nil, err
	}
	if len(items) > n {
		items = items[:n]
	}
	return items, nil
}
```

**Step 5: Verificar que pasa**

Run: `cd proxy && go test ./internal/rss/ -v`
Expected: PASS

**Step 6: Commit**

```bash
git add proxy/internal/rss/
git commit -m "proxy: fuente RSS"
```

---

### Task 4: Fuente Sorsa (Lista de X)

**Files:**
- Create: `proxy/internal/sorsa/sorsa.go`
- Create: `proxy/internal/sorsa/testdata/list_tweets.json` (viene de T0)
- Test: `proxy/internal/sorsa/sorsa_test.go`

> **Ojo:** el struct de abajo asume un shape de respuesta. Ajustalo contra el
> fixture real que capturaste en T0. El test es el que manda.

**Step 1: Test que falla**

`proxy/internal/sorsa/sorsa_test.go`:

```go
package sorsa

import (
	"os"
	"testing"
)

func TestParseListTweets(t *testing.T) {
	raw, err := os.ReadFile("testdata/list_tweets.json")
	if err != nil {
		t.Skip("falta el fixture de T0; capturalo con curl antes de correr esto")
	}
	items, err := parse(raw)
	if err != nil {
		t.Fatal(err)
	}
	if len(items) == 0 {
		t.Fatal("no parseo ningun tweet")
	}
	for _, it := range items {
		if it.ID == "" {
			t.Error("item sin ID")
		}
		if it.Text == "" {
			t.Error("item sin texto")
		}
		if it.From != "x" {
			t.Errorf("origen = %q, quiero x", it.From)
		}
		for i := 0; i < len(it.Text); i++ {
			if it.Text[i] > 127 {
				t.Fatalf("texto no normalizado: %q", it.Text)
			}
		}
		if len(it.Text) > 181 {
			t.Errorf("texto sin recortar: %d bytes", len(it.Text))
		}
	}
}
```

**Step 2: Verificar que falla**

Run: `cd proxy && go test ./internal/sorsa/`
Expected: FAIL — `undefined: parse`

**Step 3: Implementación**

`proxy/internal/sorsa/sorsa.go`:

```go
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

const endpoint = "https://api.sorsa.io/v3/list-tweets"

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

func parse(raw []byte) ([]feed.Item, error) {
	var rr rawResp
	if err := json.Unmarshal(raw, &rr); err != nil {
		return nil, err
	}
	out := make([]feed.Item, 0, len(rr.Tweets))
	for _, tw := range rr.Tweets {
		text := norm.Truncate(norm.Clean(tw.Text), 180)
		if text == "" {
			continue
		}
		at, _ := time.Parse(time.RFC3339, tw.CreatedAt)
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
	raw, err := io.ReadAll(io.LimitReader(resp.Body, 512<<10))
	if err != nil {
		return nil, err
	}
	return parse(raw)
}
```

**Step 4: Verificar que pasa**

Run: `cd proxy && go test ./internal/sorsa/ -v`
Expected: PASS (o SKIP si todavía no capturaste el fixture)

**Step 5: Commit**

```bash
git add proxy/internal/sorsa/
git commit -m "proxy: fuente Sorsa para Lista de X"
```

---

### Task 5: Mezclador con caché y tolerancia a fallos

El requisito clave: **si una fuente falla, la otra sigue sola**, y si fallan las
dos se sirve el último pool bueno.

**Files:**
- Create: `proxy/internal/mixer/mixer.go`
- Test: `proxy/internal/mixer/mixer_test.go`

**Step 1: Test que falla**

`proxy/internal/mixer/mixer_test.go`:

```go
package mixer

import (
	"errors"
	"testing"
	"time"

	"github.com/fcedeirajoaquin/ferced-display/proxy/internal/feed"
)

type fake struct {
	name  string
	items []feed.Item
	err   error
	calls int
}

func (f *fake) Name() string { return f.name }
func (f *fake) Fetch(n int) ([]feed.Item, error) {
	f.calls++
	return f.items, f.err
}

func at(h int) time.Time {
	return time.Date(2026, 8, 2, h, 0, 0, 0, time.UTC)
}

func TestMergesBothSourcesNewestFirst(t *testing.T) {
	a := &fake{name: "a", items: []feed.Item{{ID: "1", Text: "x", At: at(10)}}}
	b := &fake{name: "b", items: []feed.Item{{ID: "2", Text: "r", At: at(11)}}}
	m := New(2*time.Minute, a, b)

	got := m.Feed(10)
	if len(got) != 2 {
		t.Fatalf("items = %d, quiero 2", len(got))
	}
	if got[0].ID != "2" {
		t.Errorf("primero = %q, quiero el mas nuevo (2)", got[0].ID)
	}
}

func TestOneSourceFailingDoesNotKillFeed(t *testing.T) {
	a := &fake{name: "a", err: errors.New("caida")}
	b := &fake{name: "b", items: []feed.Item{{ID: "2", Text: "r", At: at(11)}}}
	m := New(2*time.Minute, a, b)

	got := m.Feed(10)
	if len(got) != 1 || got[0].ID != "2" {
		t.Fatalf("quiero solo el item de b, tengo %+v", got)
	}
}

func TestBothFailingServesLastGoodPool(t *testing.T) {
	a := &fake{name: "a", items: []feed.Item{{ID: "1", Text: "x", At: at(10)}}}
	m := New(0, a)
	if len(m.Feed(10)) != 1 {
		t.Fatal("primer fetch deberia traer 1")
	}

	a.items, a.err = nil, errors.New("caida")
	got := m.Feed(10)
	if len(got) != 1 || got[0].ID != "1" {
		t.Fatalf("deberia servir el pool viejo, tengo %+v", got)
	}
}

func TestCacheAvoidsRefetch(t *testing.T) {
	a := &fake{name: "a", items: []feed.Item{{ID: "1", Text: "x", At: at(10)}}}
	m := New(time.Hour, a)
	m.Feed(10)
	m.Feed(10)
	if a.calls != 1 {
		t.Fatalf("llamadas = %d, quiero 1 (la segunda sale de cache)", a.calls)
	}
}
```

**Step 2: Verificar que falla**

Run: `cd proxy && go test ./internal/mixer/`
Expected: FAIL — `undefined: New`

**Step 3: Implementación**

`proxy/internal/mixer/mixer.go`:

```go
package mixer

import (
	"log"
	"sync"
	"time"

	"github.com/fcedeirajoaquin/ferced-display/proxy/internal/feed"
)

type Mixer struct {
	ttl     time.Duration
	sources []feed.Source

	mu       sync.Mutex
	pool     []feed.Item
	fetched  time.Time
	hasPool  bool
}

func New(ttl time.Duration, sources ...feed.Source) *Mixer {
	return &Mixer{ttl: ttl, sources: sources}
}

func (m *Mixer) Feed(n int) []feed.Item {
	m.mu.Lock()
	defer m.mu.Unlock()

	if m.hasPool && time.Since(m.fetched) < m.ttl {
		return m.take(n)
	}

	var merged []feed.Item
	ok := false
	for _, s := range m.sources {
		items, err := s.Fetch(n)
		if err != nil {
			log.Printf("[mixer] %s fallo: %v", s.Name(), err)
			continue
		}
		ok = true
		merged = append(merged, items...)
	}

	if !ok {
		log.Printf("[mixer] todas las fuentes fallaron; sirvo pool viejo")
		return m.take(n)
	}

	feed.Sort(merged)
	m.pool = feed.Dedup(merged)
	m.fetched = time.Now()
	m.hasPool = true
	return m.take(n)
}

func (m *Mixer) take(n int) []feed.Item {
	if n > len(m.pool) {
		n = len(m.pool)
	}
	out := make([]feed.Item, n)
	copy(out, m.pool[:n])
	return out
}
```

**Step 4: Verificar que pasa**

Run: `cd proxy && go test ./internal/mixer/ -v`
Expected: PASS (4 tests)

**Step 5: Commit**

```bash
git add proxy/internal/mixer/
git commit -m "proxy: mezclador con cache y tolerancia a fallos"
```

---

### Task 6: Handler HTTP y main

**Files:**
- Create: `proxy/internal/api/handler.go`
- Create: `proxy/cmd/feedproxy/main.go`
- Test: `proxy/internal/api/handler_test.go`

**Step 1: Test que falla**

`proxy/internal/api/handler_test.go`:

```go
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

type fake struct{}

func (fake) Name() string { return "fake" }
func (fake) Fetch(n int) ([]feed.Item, error) {
	return []feed.Item{{
		ID: "1", Text: "hola", Author: "Alguien", Handle: "@alguien",
		At: time.Now(), Epoch: time.Now().Unix(), From: feed.OriginX,
	}}, nil
}

func TestFeedEndpointReturnsCompactJSON(t *testing.T) {
	h := New(mixer.New(time.Minute, fake{}))
	rec := httptest.NewRecorder()
	h.ServeHTTP(rec, httptest.NewRequest(http.MethodGet, "/v1/feed?n=5", nil))

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

func TestFeedClampsN(t *testing.T) {
	h := New(mixer.New(time.Minute, fake{}))
	rec := httptest.NewRecorder()
	h.ServeHTTP(rec, httptest.NewRequest(http.MethodGet, "/v1/feed?n=9999", nil))
	if rec.Code != http.StatusOK {
		t.Fatalf("status = %d", rec.Code)
	}
}
```

**Step 2: Verificar que falla**

Run: `cd proxy && go test ./internal/api/`
Expected: FAIL — `undefined: New`

**Step 3: Implementación**

`proxy/internal/api/handler.go`:

```go
package api

import (
	"encoding/json"
	"net/http"
	"strconv"

	"github.com/fcedeirajoaquin/ferced-display/proxy/internal/feed"
	"github.com/fcedeirajoaquin/ferced-display/proxy/internal/mixer"
)

const maxN = 50

type Handler struct{ mix *mixer.Mixer }

func New(m *mixer.Mixer) *Handler { return &Handler{mix: m} }

func (h *Handler) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	if r.URL.Path == "/healthz" {
		w.WriteHeader(http.StatusOK)
		w.Write([]byte("ok"))
		return
	}
	if r.URL.Path != "/v1/feed" {
		http.NotFound(w, r)
		return
	}

	n, err := strconv.Atoi(r.URL.Query().Get("n"))
	if err != nil || n <= 0 {
		n = 30
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
```

`proxy/cmd/feedproxy/main.go`:

```go
package main

import (
	"log"
	"net/http"
	"os"
	"strings"
	"time"

	"github.com/fcedeirajoaquin/ferced-display/proxy/internal/api"
	"github.com/fcedeirajoaquin/ferced-display/proxy/internal/feed"
	"github.com/fcedeirajoaquin/ferced-display/proxy/internal/mixer"
	"github.com/fcedeirajoaquin/ferced-display/proxy/internal/rss"
	"github.com/fcedeirajoaquin/ferced-display/proxy/internal/sorsa"
)

func main() {
	addr := os.Getenv("FEED_ADDR")
	if addr == "" {
		addr = "127.0.0.1:9110"
	}

	var sources []feed.Source

	if key, list := os.Getenv("SORSA_KEY"), os.Getenv("SORSA_LIST_ID"); key != "" && list != "" {
		sources = append(sources, &sorsa.Source{APIKey: key, ListID: list})
		log.Printf("fuente: Lista de X (%s)", list)
	} else {
		log.Print("SORSA_KEY/SORSA_LIST_ID sin definir; sigo solo con RSS")
	}

	for _, u := range strings.Split(os.Getenv("RSS_FEEDS"), ",") {
		u = strings.TrimSpace(u)
		if u == "" {
			continue
		}
		parts := strings.SplitN(u, "|", 2)
		label := "rss"
		if len(parts) == 2 {
			label = parts[1]
		}
		sources = append(sources, &rss.Source{URL: parts[0], Label: label})
		log.Printf("fuente: RSS %s", label)
	}

	if len(sources) == 0 {
		log.Fatal("no hay ninguna fuente configurada")
	}

	h := api.New(mixer.New(10*time.Minute, sources...))
	srv := &http.Server{
		Addr:         addr,
		Handler:      h,
		ReadTimeout:  10 * time.Second,
		WriteTimeout: 20 * time.Second,
	}
	log.Printf("escuchando en %s", addr)
	log.Fatal(srv.ListenAndServe())
}
```

**Step 4: Verificar que pasa y compila todo**

Run: `cd proxy && go test ./... && go build ./...`
Expected: PASS en todos los paquetes, build sin errores

**Step 5: Commit**

```bash
git add proxy/internal/api/ proxy/cmd/
git commit -m "proxy: endpoint /v1/feed y binario"
```

---

### Task 7: Deploy al VPS

**Files:**
- Create: `proxy/deploy/feedproxy.service`
- Modify: el `Caddyfile` del VPS

**Step 1: Compilar para Linux**

```bash
cd proxy && GOOS=linux GOARCH=amd64 go build -o feedproxy ./cmd/feedproxy
```

**Step 2: Unit de systemd**

`proxy/deploy/feedproxy.service`:

```ini
[Unit]
Description=ferced-display feed proxy
After=network-online.target

[Service]
Type=simple
User=feedproxy
Environment=FEED_ADDR=127.0.0.1:9110
EnvironmentFile=/etc/feedproxy.env
ExecStart=/opt/feedproxy/feedproxy
Restart=always
RestartSec=5
NoNewPrivileges=true
ProtectSystem=strict
ProtectHome=true
PrivateTmp=true

[Install]
WantedBy=multi-user.target
```

`/etc/feedproxy.env` en el VPS (permisos `600`, **nunca al repo**):

```
SORSA_KEY=<tu key>
SORSA_LIST_ID=<id de la lista>
RSS_FEEDS=https://www.lanacion.com.ar/arc/outboundfeeds/rss/|lanacion,https://feeds.bbci.co.uk/mundo/rss.xml|bbc
```

**Step 3: Instalar**

```bash
scp feedproxy deploy/feedproxy.service <vps>:/tmp/
ssh <vps> 'sudo useradd -r -s /usr/sbin/nologin feedproxy || true; \
  sudo mkdir -p /opt/feedproxy && sudo mv /tmp/feedproxy /opt/feedproxy/ && \
  sudo chmod +x /opt/feedproxy/feedproxy && \
  sudo mv /tmp/feedproxy.service /etc/systemd/system/ && \
  sudo systemctl daemon-reload && sudo systemctl enable --now feedproxy'
```

**Step 4: Caddy**

Agregar al Caddyfile:

```
feed.ferced.com {
    reverse_proxy 127.0.0.1:9110
}
```

Luego: `sudo systemctl reload caddy`

**Step 5: Verificar de punta a punta**

```bash
curl -s https://feed.ferced.com/healthz
curl -s "https://feed.ferced.com/v1/feed?n=3" | head -c 600
```

Expected: `ok`, y un JSON con 3 ítems de texto ASCII.

**Step 6: Commit**

```bash
git add proxy/deploy/
git commit -m "proxy: unit de systemd y notas de deploy"
```

---

## Fase B — El firmware

### Task 8: Fork y limpieza

**Files:**
- Modify: `platformio.ini`
- Delete: `src/ws_binance.*`, `src/stocks_client.*`, `src/ui_dashboard.*`, `src/ui_stocks.*`, `src/ui_v2_demo.*`, `src/v2_demo_timeline.h`

**Step 1: Apuntar el remoto al repo propio**

```bash
cd /c/Users/Ferced/Desktop/Proyects/lemon-display
git remote rename origin upstream
git remote add origin https://github.com/fcedeirajoaquin/ferced-display.git
```

**Step 2: Nuevo build env**

Agregar a `platformio.ini`:

```ini
[env:ferced_display]
extends = env:matouch_esp32s3_40
build_flags =
	${env:matouch_esp32s3_40.build_flags}
	-DFERCED_DISPLAY=1
build_src_filter =
	+<*>
	-<main.cpp>
	-<ui_dashboard.cpp>
	-<ui_stocks.cpp>
	-<ui_v2_demo.cpp>
	-<ws_binance.cpp>
	-<stocks_client.cpp>
```

**Step 3: Compilar para ver qué se rompe**

Run: `python -m platformio run -e ferced_display`
Expected: errores de símbolos faltantes. Ir sacando los `#include` y llamadas a
los módulos borrados hasta que compile limpio. Ese es el trabajo de esta tarea.

**Step 4: Commit**

```bash
git add platformio.ini src/
git commit -m "firmware: build env ferced_display sin las escenas de crypto"
```

---

### Task 9: feed_client

Calco de `src/news_client.cpp`, que ya resuelve GET con límite de bytes, caché
con TTL y degradación a caché viejo.

**Files:**
- Create: `src/feed_client.h`, `src/feed_client.cpp`
- Test: `tools/test_feed_parse.py`

**Step 1: Test que falla**

`tools/test_feed_parse.py` — valida el contrato del JSON, que es lo que puede
romperse sin avisar cuando cambia el proxy:

```python
import json
import unittest

SAMPLE = json.dumps({"items": [
    {"id": "x:1", "t": "hola mundo", "a": "Alguien",
     "h": "@alguien", "ts": 1785000000, "o": "x"},
    {"id": "https://e.com/1", "t": "un titular", "a": "bbc",
     "h": "bbc", "ts": 1784999000, "o": "rss"},
]})

REQUIRED = ("id", "t", "a", "h", "ts", "o")


class TestFeedContract(unittest.TestCase):
    def test_items_have_required_fields(self):
        for item in json.loads(SAMPLE)["items"]:
            for key in REQUIRED:
                self.assertIn(key, item)

    def test_text_is_ascii(self):
        for item in json.loads(SAMPLE)["items"]:
            self.assertTrue(item["t"].isascii(), item["t"])

    def test_text_within_budget(self):
        for item in json.loads(SAMPLE)["items"]:
            self.assertLessEqual(len(item["t"]), 181)

    def test_origin_is_known(self):
        for item in json.loads(SAMPLE)["items"]:
            self.assertIn(item["o"], ("x", "rss"))


if __name__ == "__main__":
    unittest.main()
```

**Step 2: Verificar**

Run: `python -m unittest discover -s tools -p "test_*.py"`
Expected: PASS

**Step 3: Implementar el cliente**

`src/feed_client.h`:

```cpp
#pragma once

#include <stdint.h>

#define FEED_MAX_ITEMS   30
#define FEED_TEXT_LEN    192
#define FEED_AUTHOR_LEN  48
#define FEED_HANDLE_LEN  32

struct FeedItem {
    char     text[FEED_TEXT_LEN];
    char     author[FEED_AUTHOR_LEN];
    char     handle[FEED_HANDLE_LEN];
    uint32_t epoch;
    bool     fromX;
    bool     valid;
};

enum FeedFetchResult : uint8_t {
    FEED_UPDATED,
    FEED_FRESH_CACHE,
    FEED_STALE_CACHE,
    FEED_FAILED,
};

FeedFetchResult feedFetch(FeedItem* out, uint8_t maxItems, uint8_t& count,
                          bool forceRefresh = false);
void feedGetCached(FeedItem* out, uint8_t maxItems, uint8_t& count);
```

Seguir el patrón exacto de `news_client.cpp`: `apiHttpGet` con límite de bytes,
parseo a mano buscando las claves `"t":`, `"a":`, `"h":`, `"ts":`, `"o":`, caché
estático con `millis()`, y devolver `FEED_STALE_CACHE` cuando falla la red pero
hay caché.

Endpoint en `src/config.h`:

```cpp
#define FEED_ENDPOINT   "https://feed.ferced.com/v1/feed?n=30"
#define FEED_REFRESH_MS 600000
#define FEED_ROTATE_MS   17000
```

**Step 4: Compilar**

Run: `python -m platformio run -e ferced_display`
Expected: build OK

**Step 5: Commit**

```bash
git add src/feed_client.* src/config.h tools/test_feed_parse.py
git commit -m "firmware: cliente del feed"
```

---

### Task 10: Tema Ferced como tercera paleta

**No se reescribe el sistema de color.** `src/colors.h` ya tiene
`struct ThemePalette` con ~55 tokens y `Colors::setTheme()`. Ferced entra como
un tema más, al lado de `THEME_DARK` y `THEME_LIGHT`.

**Files:**
- Modify: `src/colors.h` (agregar `THEME_FERCED = 2` al enum)
- Modify: `src/colors.cpp` (agregar la entrada de paleta)

**Step 1: Mapear los tokens de Ferced a RGB565**

| Token de marca | Hex | RGB565 | Campos de `ThemePalette` |
|---|---|---|---|
| canvas | `#0e1011` | `0x0842` | `bgBase` |
| card | `#0a0a0a` | `0x0841` | `bgCard`, `bgSurface` |
| card-alt | `#0c0c0c` | `0x0861` | `bgElevated` |
| fg | `#ffffff` | `0xFFFF` | `textPrimary` |
| fg-2 (75%) | `#bfbfbf` | `0xBDF7` | `textSecondary` |
| fg-3 (55%) | `#8c8c8c` | `0x8C71` | `textTertiary` |
| fg-4 (40%) | `#666666` | `0x632C` | `textDisabled` |
| line (10%) | `#1a1a1a` | `0x18E3` | `cardBorder`, `divider` |
| line-hover (25%) | `#404040` | `0x4208` | `cardBorderAccent` |
| success | `#34d399` | `0x36F3` | `positive` |
| danger | `#f87171` | `0xFB8E` | `negative` |

Regla de la marca: **no inventar grises.** Todo gris es blanco con alpha sobre
el canvas. Verde y rojo **sólo para estado**, jamás decorativos — donde la
paleta Lemon usa `lemonGreen` como acento, Ferced usa `textPrimary` (blanco).

Los tokens de crypto (`coinBtc`, `candleBull`, `domEth`, etc.) se llenan con
blancos y grises: el struct los exige pero el firmware Ferced no los dibuja.

**Step 2: Hacerlo el tema por defecto**

En el arranque, cuando `FERCED_DISPLAY` esté definido, forzar
`Colors::setTheme(Colors::THEME_FERCED)` antes del primer dibujado.

**Step 3: Compilar y commitear**

```bash
python -m platformio run -e ferced_display
git add src/colors.*
git commit -m "firmware: paleta Ferced como tercer tema"
```

---

### Task 11: Logo y wordmark de Ferced en blanco

El firmware ya dibuja el wordmark de Lemon como bitmap RGB565 de 120x28
(`src/data/lemon_v2_logo_light_120.h`). Ferced ocupa el mismo lugar.

**Files:**
- Create: `src/data/ferced_logo_white_120.h`

**Step 1: Convertir el logo**

`logo-white.png` es blanco sólido sobre transparente, que es exactamente lo que
el conversor espera (compositea sobre negro y usa `0x0000` como clave de
transparencia).

```bash
python tools/png_to_rgb565.py \
  "C:/Users/Ferced/Desktop/Proyects/Ferced/ferced-landing-page/public/logo-white.png" \
  src/data/ferced_logo_white_120.h 120 28 ferced_logo_white_120
```

**Step 2: Verificar la proporción**

Si el logo no es 120x28 de origen, la resize lo va a deformar. Comprobar la
relación de aspecto real primero y ajustar el alto para conservarla; el ancho
de 120 px es el que ya reserva el layout.

**Step 3: Dibujarlo**

Reemplazar las llamadas al bitmap de Lemon por
`drawBitmapTransparent(gfx, x, y, ferced_logo_white_120, 120, 28)`. Al ser
blanco puro sobre `bgBase` casi negro, no hace falta tratamiento extra.

**Step 4: Commit**

```bash
git add src/data/ferced_logo_white_120.h src/ui_ferced.*
git commit -m "firmware: wordmark de Ferced en blanco"
```

---

### Task 12: La escena del feed

**Conservar el lenguaje visual actual.** La UI V2 no se reemplaza por una
pantalla de texto plano: se reusan sus primitivas, que son las que le dan el
aire moderno.

**Files:**
- Create: `src/ui_ferced.h`, `src/ui_ferced.cpp`
- Reference: `src/ui_v2_runtime.cpp` (el patrón a copiar)

**Primitivas que hay que reusar, no reinventar:**

| Primitiva | Dónde está | Para qué |
|---|---|---|
| `LGFX_Sprite` de pantalla completa en PSRAM | `ui_v2_runtime.cpp:88` | Doble buffer, cero parpadeo |
| `pushSprite` con `setClipRect` | `:224-250` | Redibujar sólo lo que cambia |
| `fillSmoothRoundRect` | `:273` | Píldoras y tarjetas antialiaseadas |
| `drawWideLine` con ancho float | `:165, 255-262` | Líneas y glifos suaves |
| `drawBitmapTransparent` | `data/market_icons.h:22` | Logo |

**Layout 480x480:**

```
┌──────────────────────────────────────┐
│  ╭─────────────╮            ferced   │  chip píldora + wordmark 120x28
│  │ AHORA · @han│                     │  fillSmoothRoundRect, Satoshi9
│  ╰─────────────╯                     │
│  ╭────────────────────────────────╮  │  tarjeta: fillSmoothRoundRect
│  │                                │  │  radio 16, bgCard, borde cardBorder
│  │  El texto del item, en         │  │  SatoshiMedium18, textPrimary
│  │  Satoshi, hasta cinco lineas.  │  │  wrap por ancho real con textWidth()
│  │                                │  │
│  │  Nombre Apellido               │  │  Satoshi12, textSecondary
│  │  hace 12 min                   │  │  Satoshi9, textTertiary
│  ╰────────────────────────────────╯  │
│         ● ● ○ ○ ○ ○ ○ ○              │  puntos de posicion en el pool
└──────────────────────────────────────┘
```

**Detalles que hacen la diferencia:**

- **Wrap por ancho real, no por conteo de caracteres.** Usar
  `sprite.textWidth(palabra, fuente)` y cortar por píxeles. Contar caracteres
  con fuente proporcional deja renglones desparejos.
- **Chip de origen**: `fillSmoothRoundRect` con `bgSurface` y texto
  `textTertiary`. Dice `AHORA · @handle` para X, `AHORA · BBC` para RSS.
- **Transición entre ítems**: fade corto reusando el patrón de transición por
  clip que ya existe, no un corte seco.
- **Puntos de posición**: `fillCircle` chiquitos, el actual en `textPrimary` y
  el resto en `textDisabled`. Dan sensación de avance.
- **Tiempo relativo** recalculado en cada dibujado desde `epoch` y la hora NTP
  (`hace X min` / `hace X h`). Es lo que evita que la repetición se note.

**Step 1: Compilar**

```bash
python -m platformio run -e ferced_display
```

**Step 2: Commit**

```bash
git add src/ui_ferced.*
git commit -m "firmware: escena del feed con identidad Ferced"
```

---

### Task 13: Rotación en el runtime

**Files:**
- Modify: `src/v2_runtime.cpp`

Dos temporizadores en el scheduler cooperativo que ya existe:

- Cada `FEED_ROTATE_MS` (17 s): avanzar al siguiente ítem del pool. No repetir
  hasta agotarlo; al agotarse, volver al principio.
- Cada `FEED_REFRESH_MS` (10 min): `feedFetch()`. Si devuelve `FEED_STALE_CACHE`,
  dibujar la marca sutil de desconexión y reintentar con backoff 5→60 s.

Sacar de `v2RuntimeSetup()` todo lo que arranque WebSocket de Binance, stocks y
Polymarket.

**Commit:**

```bash
git add src/v2_runtime.cpp
git commit -m "firmware: rotacion del feed y refresco"
```

---

### Task 14: OTA propio

**Files:**
- Modify: `src/config.h`

```cpp
#define OTA_GITHUB_REPO "fcedeirajoaquin/ferced-display"
#define OTA_V2_ASSET    "firmware-ferced.bin"
#define APP_VERSION     "1.0.0-beta.1"
```

**Commit:**

```bash
git add src/config.h
git commit -m "firmware: canal OTA propio"
```

---

### Task 15: Flashear y verificar

**Step 1: Build**

```bash
python -m platformio run -e ferced_display
```

**Step 2: Backup del firmware actual**

El equipo hoy corre beta.74 de Lemon. Antes de pisarlo:

```bash
python -m esptool --chip esp32s3 --port COM3 --baud 921600 \
  --before default-reset --after no-reset \
  read-flash 0x10000 0x640000 rollback-lemon-beta74.bin
```

**Step 3: Flashear**

La tabla de particiones del equipo ya está en el layout de 16 MB (se corrigió
el 2026-08-02, ver `output/release-beta74-*/ROLLBACK.md`), así que alcanza con
escribir la app:

```bash
python -m esptool --chip esp32s3 --port COM3 --baud 921600 \
  --before default-reset --after hard-reset \
  write-flash --flash-mode keep --flash-size keep \
  0x10000 .pio/build/ferced_display/firmware.bin
```

**CRÍTICO:** siempre `--flash-mode keep --flash-size keep`. Forzar `qio` en este
ESP32-S3 causa boot loop.

**Step 4: Verificar el arranque**

Puerto estable 20 s = OK. Puerto que aparece y desaparece = boot loop, rehacer
con el rollback.

```bash
python -m esptool --chip esp32s3 --port COM3 --baud 921600 \
  --before default-reset --after hard-reset \
  verify-flash --flash-mode keep --flash-size keep \
  0x10000 .pio/build/ferced_display/firmware.bin
```

Confirmación visual: la pantalla debe mostrar el eyebrow `AHORA · @handle`, un
texto en Satoshi blanco sobre negro, y cambiar de ítem cada 17 segundos.

**Step 5: Commit y primer release**

```bash
git push -u origin feat/ferced-display
certutil -hashfile .pio/build/ferced_display/firmware.bin MD5
gh release create v1.0.0-beta.1 --repo fcedeirajoaquin/ferced-display \
  .pio/build/ferced_display/firmware.bin#firmware-ferced.bin \
  --notes "firmware-ferced.bin MD5: <hash>"
```

---

## Notas de verificación

Antes de cantar victoria en cualquier tarea, correr de verdad:

```bash
cd proxy && go test ./...
python -m unittest discover -s tools -p "test_*.py"
python -m platformio run -e ferced_display
```

@superpowers:verification-before-completion — evidencia antes que afirmaciones.

## Fuera de alcance (etapa 2)

- Fuente Latin-1 con tildes reales vía `tools/ttf_to_gfx.py`
- Renombrar el AP de provisioning de `Lemon-Setup` a `Ferced-Setup`
- Segunda escena con métricas propias de Ferced
